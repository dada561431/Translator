#include "ocr/TesseractOcrEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <QStringList>

#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

struct TessBaseAPI;

using CreateFn = TessBaseAPI *(*)();
using DeleteFn = void (*)(TessBaseAPI *);
using InitFn = int (*)(TessBaseAPI *, const char *, const char *);
using EndFn = void (*)(TessBaseAPI *);
using SetImageFn = void (*)(TessBaseAPI *, const unsigned char *, int, int, int, int);
using GetTextFn = char *(*)(TessBaseAPI *);
using DeleteTextFn = void (*)(const char *);
using ClearFn = void (*)(TessBaseAPI *);
using SetPageSegModeFn = void (*)(TessBaseAPI *, int);

QStringList engineDirectories()
{
    QStringList directories;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty())
        directories << appDir;

    const auto environment = QProcessEnvironment::systemEnvironment();
    for (const QString &entry : environment.value(QStringLiteral("PATH")).split(QDir::listSeparator(), Qt::SkipEmptyParts))
        directories << QDir::fromNativeSeparators(entry);

#ifdef Q_OS_WIN
    for (const QString &root : {environment.value(QStringLiteral("ProgramFiles")),
                                environment.value(QStringLiteral("ProgramFiles(x86)")),
                                QStringLiteral("C:/Program Files")}) {
        if (!root.isEmpty())
            directories << QDir(root).filePath(QStringLiteral("Tesseract-OCR"));
    }
#endif
    directories.removeDuplicates();
    return directories;
}

QString findLibrary()
{
#ifdef Q_OS_WIN
    const QStringList names = {QStringLiteral("libtesseract-5.dll"),
                               QStringLiteral("libtesseract.dll")};
#else
    const QStringList names = {QStringLiteral("libtesseract.so.5"),
                               QStringLiteral("libtesseract.so")};
#endif
    for (const QString &directory : engineDirectories()) {
        for (const QString &name : names) {
            const QString path = QDir(directory).filePath(name);
            if (QFileInfo::exists(path))
                return path;
        }
    }
    return names.first();
}

QString findDataDirectory(const QString &libraryPath)
{
    QStringList candidates;
    const auto environment = QProcessEnvironment::systemEnvironment();
    const QString prefix = environment.value(QStringLiteral("TESSDATA_PREFIX"));
    if (!prefix.isEmpty()) {
        candidates << prefix;
        candidates << QDir(prefix).filePath(QStringLiteral("tessdata"));
    }
    const QString localAppData = environment.value(QStringLiteral("LOCALAPPDATA"));
    if (!localAppData.isEmpty())
        candidates << QDir(localAppData).filePath(QStringLiteral("Tesseract-OCR/tessdata"));
    const QString libraryDirectory = QFileInfo(libraryPath).absolutePath();
    candidates << QDir(libraryDirectory).filePath(QStringLiteral("tessdata"));
    candidates << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tessdata"));
    candidates.removeDuplicates();
    for (const QString &candidate : candidates) {
        if (QDir(candidate).exists() && !QDir(candidate).entryList({QStringLiteral("*.traineddata")}, QDir::Files).isEmpty())
            return QDir(candidate).absolutePath();
    }
    return {};
}

QStringList requestedLanguages(const QString &hint)
{
    if (hint == QLatin1String("zh"))
        return {QStringLiteral("chi_sim")};
    if (hint == QLatin1String("en"))
        return {QStringLiteral("eng")};
    if (hint == QLatin1String("ja"))
        return {QStringLiteral("jpn")};
    if (hint == QLatin1String("ko"))
        return {QStringLiteral("kor")};
    if (hint == QLatin1String("auto"))
        return {QStringLiteral("eng")};
    return {};
}

} // namespace

struct TesseractOcrEngine::Impl
{
    QMutex mutex;
    QLibrary library;
    TessBaseAPI *api = nullptr;
    CreateFn create = nullptr;
    DeleteFn destroy = nullptr;
    InitFn init = nullptr;
    EndFn end = nullptr;
    SetImageFn setImage = nullptr;
    GetTextFn getText = nullptr;
    DeleteTextFn deleteText = nullptr;
    ClearFn clear = nullptr;
    SetPageSegModeFn setPageSegMode = nullptr;
    QString libraryPath;
    QString dataDirectory;
    QString initializedLanguages;
    QString loadError;
    QString configuredDataDirectory;

    explicit Impl(QString tessdataPath)
        : configuredDataDirectory(std::move(tessdataPath))
    {
    }

    ~Impl()
    {
        if (api) {
            end(api);
            destroy(api);
        }
    }

    bool load()
    {
        if (api)
            return true;
        libraryPath = findLibrary();
        library.setFileName(libraryPath);
#ifdef Q_OS_WIN
        // Preload with the DLL's own directory so its bundled dependencies resolve.
        const std::wstring nativePath = QDir::toNativeSeparators(libraryPath).toStdWString();
        HMODULE preload = LoadLibraryExW(nativePath.c_str(), nullptr,
                                         LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!preload) {
            loadError = QStringLiteral("Tesseract 5 DLL unavailable: %1 (Windows error %2)")
                            .arg(libraryPath).arg(GetLastError());
            return false;
        }
#endif
        const bool loaded = library.load();
#ifdef Q_OS_WIN
        FreeLibrary(preload);
#endif
        if (!loaded) {
            loadError = QStringLiteral("Tesseract 5 DLL unavailable: %1 (%2)")
                            .arg(libraryPath, library.errorString());
            return false;
        }

        create = reinterpret_cast<CreateFn>(library.resolve("TessBaseAPICreate"));
        destroy = reinterpret_cast<DeleteFn>(library.resolve("TessBaseAPIDelete"));
        init = reinterpret_cast<InitFn>(library.resolve("TessBaseAPIInit3"));
        end = reinterpret_cast<EndFn>(library.resolve("TessBaseAPIEnd"));
        setImage = reinterpret_cast<SetImageFn>(library.resolve("TessBaseAPISetImage"));
        getText = reinterpret_cast<GetTextFn>(library.resolve("TessBaseAPIGetUTF8Text"));
        deleteText = reinterpret_cast<DeleteTextFn>(library.resolve("TessDeleteText"));
        clear = reinterpret_cast<ClearFn>(library.resolve("TessBaseAPIClear"));
        setPageSegMode = reinterpret_cast<SetPageSegModeFn>(
            library.resolve("TessBaseAPISetPageSegMode"));
        if (!create || !destroy || !init || !end || !setImage || !getText
            || !deleteText || !clear || !setPageSegMode) {
            loadError = QStringLiteral("Tesseract DLL lacks a required C API symbol: %1")
                            .arg(library.errorString());
            library.unload();
            return false;
        }
        api = create();
        if (!api) {
            loadError = QStringLiteral("Tesseract API allocation failed");
            library.unload();
            return false;
        }
        dataDirectory = configuredDataDirectory.isEmpty()
            ? findDataDirectory(libraryPath)
            : QDir(configuredDataDirectory).absolutePath();
        return true;
    }
};

TesseractOcrEngine::TesseractOcrEngine(const QString &tessdataPath)
    : impl_(std::make_unique<Impl>(tessdataPath))
{
}

TesseractOcrEngine::~TesseractOcrEngine() = default;

QString TesseractOcrEngine::id() const
{
    return QStringLiteral("tesseract");
}

OcrResult TesseractOcrEngine::recognize(const QImage &image, const QString &sourceLanguage)
{
    return recognizeWithOptions(image, sourceLanguage, OcrPreprocessOptions());
}

OcrResult TesseractOcrEngine::recognizeWithOptions(
    const QImage &image, const QString &sourceLanguage,
    const OcrPreprocessOptions &options, int pageSegmentationMode)
{
    QElapsedTimer timer;
    timer.start();
    OcrResult result;
    result.engineId = id();
    result.inputSize = image.size();
    const auto finish = [&]() {
        result.elapsedMs = timer.elapsed();
        return result;
    };

    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        result.error = QStringLiteral("OCR image is empty");
        return finish();
    }
    const QStringList requested = requestedLanguages(sourceLanguage.trimmed().toLower());
    if (requested.isEmpty()) {
        result.error = QStringLiteral("Unsupported OCR language hint: %1").arg(sourceLanguage);
        return finish();
    }

    QMutexLocker locker(&impl_->mutex);
    if (!impl_->load()) {
        result.error = impl_->loadError;
        return finish();
    }
    if (impl_->dataDirectory.isEmpty()) {
        result.error = QStringLiteral("Tesseract tessdata directory was not found; set TESSDATA_PREFIX");
        return finish();
    }

    QStringList available;
    QStringList missing;
    for (const QString &language : requested) {
        if (QFileInfo::exists(QDir(impl_->dataDirectory).filePath(language + QStringLiteral(".traineddata"))))
            available << language;
        else
            missing << language;
    }
    result.tesseractLanguage = requested.join(QLatin1Char('+'));
    result.tessdataPath = impl_->dataDirectory;
    if (!missing.isEmpty()) {
        result.error = QStringLiteral("Missing Tesseract language data: %1 in %2")
                           .arg(missing.join(QStringLiteral(", ")), impl_->dataDirectory);
        return finish();
    }

    const QString languages = available.join(QLatin1Char('+'));
    result.tesseractLanguage = languages;
    if (impl_->initializedLanguages != languages) {
        if (!impl_->initializedLanguages.isEmpty())
            impl_->end(impl_->api);
        const QByteArray dataPath = QDir::toNativeSeparators(impl_->dataDirectory).toUtf8();
        const QByteArray languageNames = languages.toUtf8();
        if (impl_->init(impl_->api, dataPath.constData(), languageNames.constData()) != 0) {
            impl_->initializedLanguages.clear();
            result.error = QStringLiteral("Tesseract failed to initialize %1 from %2")
                               .arg(languages, impl_->dataDirectory);
            return finish();
        }
        impl_->initializedLanguages = languages;
    }

    const OcrPreprocessResult preprocessing = OcrImagePreprocessor::process(image, options);
    result.preprocessingMode = preprocessing.mode;
    result.preprocessingMs = preprocessing.elapsedMs;
    result.processedSize = preprocessing.image.size();
    const QImage pixels = preprocessing.image.format() == QImage::Format_Grayscale8
        ? preprocessing.image
        : preprocessing.image.convertToFormat(QImage::Format_RGB888);
    if (pixels.isNull()) {
        result.error = QStringLiteral("OCR image conversion failed");
        return finish();
    }
    const int psm = pageSegmentationMode > 0
        ? pageSegmentationMode
        : OcrImagePreprocessor::selectPageSegmentationMode(image.size());
    result.pageSegmentationMode = psm;
    impl_->setPageSegMode(impl_->api, psm);
    const int bytesPerPixel = pixels.format() == QImage::Format_Grayscale8 ? 1 : 3;
    impl_->setImage(impl_->api, pixels.constBits(), pixels.width(), pixels.height(),
                    bytesPerPixel, pixels.bytesPerLine());
    QElapsedTimer recognitionTimer;
    recognitionTimer.start();
    char *recognized = impl_->getText(impl_->api);
    result.recognitionMs = recognitionTimer.elapsed();
    if (!recognized) {
        impl_->clear(impl_->api);
        result.error = QStringLiteral("Tesseract recognition failed");
        return finish();
    }
    result.text = QString::fromUtf8(recognized).trimmed();
    impl_->deleteText(recognized);
    impl_->clear(impl_->api);
    return finish();
}
