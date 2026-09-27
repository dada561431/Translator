#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QTextStream>

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
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    if (application.arguments().size() < 3) {
        output << "usage: TranslatorOcrBenchmark <image> <auto|zh|en|ja|ko>\n";
        return 2;
    }

    const QString imagePath = application.arguments().at(1);
    const QString language = application.arguments().at(2);
    const QImage image(imagePath);
    if (image.isNull()) {
        output << "Could not load image: " << QFileInfo(imagePath).absoluteFilePath() << '\n';
        return 2;
    }

    TesseractOcrEngine engine;
    OcrPreprocessOptions baselineOptions;
    baselineOptions.mode = OcrPreprocessMode::Original;
    OcrPreprocessOptions optimizedOptions;
    optimizedOptions.mode = OcrPreprocessMode::Automatic;
    const OcrResult baseline6 = engine.recognizeWithOptions(image, language, baselineOptions, 6);
    const OcrResult baseline7 = engine.recognizeWithOptions(image, language, baselineOptions, 7);
    const OcrResult optimized6 = engine.recognizeWithOptions(image, language, optimizedOptions, 6);
    const OcrResult optimized7 = engine.recognizeWithOptions(image, language, optimizedOptions, 7);
    const OcrResult optimized = engine.recognize(image, language);

    output << "variant\tinput\tprocessed\tpreprocess\tpsm\tpre_ms\tocr_ms\ttotal_ms\ttext\terror\n";
    printResult(output, QStringLiteral("before-psm6"), baseline6);
    printResult(output, QStringLiteral("before-psm7"), baseline7);
    printResult(output, QStringLiteral("after-psm6"), optimized6);
    printResult(output, QStringLiteral("after-psm7"), optimized7);
    printResult(output, QStringLiteral("after-auto"), optimized);
    return optimized.isValid() ? 0 : 1;
}
