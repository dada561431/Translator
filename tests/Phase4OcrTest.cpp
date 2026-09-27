#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>
#include <memory>

#include "app/OcrCoordinator.h"
#include "config/SettingsManager.h"
#include "gui/TranslationWindow.h"
#include "ocr/IOcrEngine.h"

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class FakeOcrEngine final : public IOcrEngine
{
public:
    QString id() const override { return QStringLiteral("fake_ocr"); }

    OcrResult recognize(const QImage &image, const QString &sourceLanguage) override
    {
        ++calls;
        lastLanguage = sourceLanguage;
        lastImageSize = image.size();

        OcrResult result;
        result.engineId = id();
        result.elapsedMs = 7;
        if (image.isNull()) {
            result.error = QStringLiteral("Invalid image");
        } else if (image.pixelColor(0, 0) == Qt::white) {
            result.text.clear();
        } else {
            result.text = QStringLiteral("recognized sample");
        }
        return result;
    }

    int calls = 0;
    QString lastLanguage;
    QSize lastImageSize;
};

OcrResult awaitResult(OcrCoordinator &coordinator, const CaptureResult &capture,
                      const QString &language, bool &received)
{
    OcrResult result;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    const auto connection = QObject::connect(
        &coordinator, &OcrCoordinator::resultReady, &loop,
        [&](const OcrResult &value) {
            result = value;
            received = true;
            loop.quit();
        });
    coordinator.recognize(capture, language);
    timeout.start(2000);
    loop.exec();
    QObject::disconnect(connection);
    return result;
}

} // namespace

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TranslatorPhase4Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("OcrTest"));

    QTemporaryDir directory;
    check(directory.isValid(), "temporary settings directory exists");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());

    FakeOcrEngine engine;
    check(engine.id() == QStringLiteral("fake_ocr"), "fake engine has a stable ID");

    const OcrResult invalid = engine.recognize(QImage(), QStringLiteral("auto"));
    check(!invalid.isValid() && !invalid.error.isEmpty(), "invalid image reports an error");
    check(invalid.engineId == engine.id(), "invalid result records engine ID");
    check(engine.lastLanguage == QStringLiteral("auto"), "auto hint reaches engine unchanged");

    QImage blank(8, 6, QImage::Format_ARGB32);
    blank.fill(Qt::white);
    const OcrResult empty = engine.recognize(blank, QStringLiteral("en"));
    check(empty.isValid() && empty.text.isEmpty(), "blank image has valid empty result");
    check(engine.lastImageSize == blank.size(), "engine receives original image dimensions");
    check(engine.lastLanguage == QStringLiteral("en"), "source language reaches engine");
    check(empty.elapsedMs >= 0, "elapsed time is nonnegative");

    QImage content(8, 6, QImage::Format_ARGB32);
    content.fill(Qt::black);
    const OcrResult recognized = engine.recognize(content, QStringLiteral("ja"));
    check(recognized.isValid() && recognized.text == QStringLiteral("recognized sample"),
          "fake engine returns deterministic text");
    check(engine.calls == 3, "fake engine records each recognition request");

    OcrCoordinator coordinator([] { return std::make_unique<FakeOcrEngine>(); });
    CaptureResult capture;
    capture.image = content;
    bool received = false;
    const OcrResult coordinated = awaitResult(coordinator, capture, QStringLiteral("ja"),
                                              received);
    check(received, "coordinator emits a resultReady signal");
    check(coordinated.isValid() && coordinated.text == recognized.text,
          "coordinator forwards fake OCR text");
    check(coordinated.engineId == engine.id(), "coordinator preserves engine ID");

    capture.image = blank;
    received = false;
    const OcrResult coordinatedBlank = awaitResult(coordinator, capture,
                                                   QStringLiteral("en"), received);
    check(received && coordinatedBlank.isValid() && coordinatedBlank.text.isEmpty(),
          "coordinator emits a valid empty result for blank input");

    capture.image = QImage();
    received = false;
    const OcrResult coordinatedInvalid = awaitResult(coordinator, capture,
                                                     QStringLiteral("auto"), received);
    check(received && !coordinatedInvalid.isValid(),
          "coordinator forwards invalid-image errors");

    OcrCoordinator unavailable({});
    received = false;
    const OcrResult missingEngine = awaitResult(unavailable, capture,
                                                QStringLiteral("auto"), received);
    check(received && !missingEngine.isValid() && !missingEngine.error.isEmpty(),
          "missing engine emits an error result");

    SettingsManager settings;
    TranslationWindow window(settings);
    window.show();
    application.processEvents();
    auto *original = window.findChild<QLabel *>(QStringLiteral("originalLabel"));
    auto *translated = window.findChild<QLabel *>(QStringLiteral("translatedLabel"));
    check(original != nullptr && translated != nullptr, "subtitle labels exist");
    if (original && translated) {
        const QString translatedPlaceholder = translated->text();
        const QString originalPlaceholder = original->text();
        window.setOriginalText(QString());
        check(original->text() == originalPlaceholder,
              "empty content preserves the original placeholder");
        window.setOriginalText(recognized.text);
        check(original->text() == recognized.text, "OCR text reaches original subtitle setter");
        check(translated->text() == translatedPlaceholder,
              "original subtitle update leaves translation unchanged");
        window.setOriginalText(QString());
        check(original->text().isEmpty(),
              "empty content clears original text after the placeholder is replaced");

        QObject::connect(&coordinator, &OcrCoordinator::resultReady, &window,
                         [&window](const OcrResult &result) {
                             if (result.isValid()) {
                                 window.setOriginalText(result.text);
                             }
                         });
        capture.image = content;
        received = false;
        const OcrResult displayed = awaitResult(coordinator, capture,
                                                QStringLiteral("ja"), received);
        check(received && displayed.isValid() && original->text() == displayed.text,
              "coordinator result reaches original subtitle setter");
        check(translated->text() == translatedPlaceholder,
              "OCR signal does not modify translated subtitle");
        capture.image = blank;
        received = false;
        const OcrResult cleared = awaitResult(coordinator, capture,
                                              QStringLiteral("en"), received);
        check(received && cleared.isValid() && original->text().isEmpty(),
              "valid blank OCR result clears prior original subtitle text");
    }
    window.close();

    if (failures == 0) {
        std::cout << "All Phase 4 OCR contract checks passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
