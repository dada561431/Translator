#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>
#include "ocr/TesseractOcrEngine.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {
qint64 workingSet()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return qint64(counters.WorkingSetSize);
#endif
    return -1;
}
bool writeJson(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(object).toJson();
    return file.write(bytes) == bytes.size();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 3) return 2;
    QFile requestFile(args.at(1));
    if (!requestFile.open(QIODevice::ReadOnly)) return 2;
    QJsonParseError parseError;
    const auto requestDoc = QJsonDocument::fromJson(requestFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !requestDoc.isObject()) return 2;
    const auto request = requestDoc.object();
    const int repeats = qBound(1, request.value("repeats").toInt(3), 100);
    QJsonObject response{{"engine", "production_tesseract"}, {"memory_before_bytes", workingSet()}};
    QElapsedTimer time; time.start();
    auto engine = std::make_unique<TesseractOcrEngine>();
    response.insert("constructor_ms", double(time.nsecsElapsed()) / 1e6);
    response.insert("lazy_initialization", true);
    QJsonArray rows;
    bool first = true;
    for (const auto value : request.value("samples").toArray()) {
        const auto sample = value.toObject();
        const QImage image(sample.value("path").toString());
        const QString language = sample.value("language").toString();
        time.restart();
        OcrResult result = engine->recognize(image, language);
        const double firstCall = double(time.nsecsElapsed()) / 1e6;
        if (first) {
            // The production engine initializes lazily: ready includes one inference.
            response.insert("cold_ready_including_first_inference_ms",
                            response.value("constructor_ms").toDouble() + firstCall);
            response.insert("memory_after_ready_bytes", workingSet());
            first = false;
        }
        QJsonArray times, outputs;
        QString error = result.error;
        if (result.isValid()) {
            for (int i = 0; i < repeats; ++i) {
                time.restart();
                result = engine->recognize(image, language);
                times.append(double(time.nsecsElapsed()) / 1e6);
                outputs.append(result.text);
                if (!result.isValid()) { error = result.error; break; }
            }
        }
        rows.append(QJsonObject{{"filename", sample.value("filename")}, {"text", result.text},
            {"error", error}, {"warmup_ms", firstCall}, {"times_ms", times}, {"run_texts", outputs},
            {"input_width", image.width()}, {"input_height", image.height()},
            {"processed_width", result.processedSize.width()}, {"processed_height", result.processedSize.height()},
            {"preprocess", result.preprocessingMode}, {"psm", result.pageSegmentationMode},
            {"tesseract_language", result.tesseractLanguage}, {"tessdata", result.tessdataPath},
            {"preprocessing_ms", result.preprocessingMs}, {"recognition_ms", result.recognitionMs}});
    }
    response.insert("memory_after_all_bytes", workingSet());
    response.insert("rows", rows);
    return writeJson(args.at(2), response) ? 0 : 2;
}
