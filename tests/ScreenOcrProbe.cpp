#include <QGuiApplication>
#include <QScreen>
#include <QTextStream>

#include "capture/ScreenCaptureService.h"
#include "ocr/TesseractOcrEngine.h"

namespace {

void printResult(QTextStream &output, const QString &label, const OcrResult &result)
{
    output << label << '\t'
           << result.inputSize.width() << 'x' << result.inputSize.height() << '\t'
           << result.processedSize.width() << 'x' << result.processedSize.height() << '\t'
           << result.preprocessingMode << '\t'
           << result.pageSegmentationMode << '\t'
           << result.preprocessingMs << '\t'
           << result.recognitionMs << '\t'
           << result.elapsedMs << '\t'
           << result.text.simplified() << '\t'
           << result.error.simplified() << '\n';
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QTextStream output(stdout);
    const QStringList arguments = application.arguments();
    if (arguments.size() < 7) {
        output << "usage: TranslatorScreenOcrProbe <x> <y> <w> <h> <png> <language>\n";
        return 2;
    }

    const QRect rect(arguments.at(1).toInt(), arguments.at(2).toInt(),
                     arguments.at(3).toInt(), arguments.at(4).toInt());
    QScreen *screen = QGuiApplication::screenAt(rect.center());
    if (!screen) {
        output << "No screen contains the requested rectangle\n";
        return 2;
    }

    const CaptureResult capture = ScreenCaptureService().capture(screen, rect);
    if (!capture.isValid()) {
        output << "Capture failed: " << capture.error << '\n';
        return 1;
    }
    if (!capture.image.save(arguments.at(5), "PNG")) {
        output << "Could not save capture\n";
        return 1;
    }

    TesseractOcrEngine engine;
    OcrPreprocessOptions original;
    original.mode = OcrPreprocessMode::Original;
    const OcrResult before6 = engine.recognizeWithOptions(capture.image, arguments.at(6), original, 6);
    const OcrResult before7 = engine.recognizeWithOptions(capture.image, arguments.at(6), original, 7);
    const OcrResult after = engine.recognize(capture.image, arguments.at(6));
    output << "variant\tinput\tprocessed\tpreprocess\tpsm\tpre_ms\tocr_ms\ttotal_ms\ttext\terror\n";
    printResult(output, QStringLiteral("before-psm6"), before6);
    printResult(output, QStringLiteral("before-psm7"), before7);
    printResult(output, QStringLiteral("after-auto"), after);
    return after.isValid() ? 0 : 1;
}
