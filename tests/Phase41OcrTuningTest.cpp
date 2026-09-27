#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>

#include <iostream>

#include "ocr/OcrImagePreprocessor.h"
#include "ocr/TesseractOcrEngine.h"

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);

    QImage source(100, 30, QImage::Format_ARGB32);
    source.fill(QColor(40, 90, 140));
    const QImage originalCopy = source.copy();

    OcrPreprocessOptions upscaleOptions;
    upscaleOptions.mode = OcrPreprocessMode::Upscale;
    upscaleOptions.scaleFactor = 2;
    const auto upscaled = OcrImagePreprocessor::process(source, upscaleOptions);
    check(upscaled.image.size() == QSize(200, 60), "explicit 2x upscale has exact size");
    check(source == originalCopy, "preprocessing does not modify the source image");

    OcrPreprocessOptions grayscaleOptions;
    grayscaleOptions.mode = OcrPreprocessMode::UpscaleGrayscale;
    grayscaleOptions.scaleFactor = 1;
    const auto grayscale = OcrImagePreprocessor::process(source, grayscaleOptions);
    check(grayscale.image.format() == QImage::Format_Grayscale8,
          "grayscale mode returns Grayscale8");

    QImage gradient(256, 16, QImage::Format_Grayscale8);
    for (int y = 0; y < gradient.height(); ++y) {
        uchar *line = gradient.scanLine(y);
        for (int x = 0; x < gradient.width(); ++x) {
            line[x] = uchar(70 + x / 3);
        }
    }
    OcrPreprocessOptions contrastOptions;
    contrastOptions.mode = OcrPreprocessMode::UpscaleGrayscaleContrast;
    contrastOptions.scaleFactor = 1;
    const auto contrast = OcrImagePreprocessor::process(gradient, contrastOptions);
    check(!contrast.image.isNull() && contrast.image.format() == QImage::Format_Grayscale8,
          "contrast enhancement safely returns grayscale pixels");

    const auto empty = OcrImagePreprocessor::process(QImage());
    check(empty.image.isNull(), "empty image preprocessing is safe");
    check(OcrImagePreprocessor::recommendedScale(QSize(100, 30)) == 3,
          "very small text region uses 3x scale");
    check(OcrImagePreprocessor::recommendedScale(QSize(210, 68)) == 2,
          "small text region uses 2x scale");
    check(OcrImagePreprocessor::recommendedScale(QSize(800, 180)) == 1,
          "large text region is not enlarged");
    check(OcrImagePreprocessor::selectPageSegmentationMode(QSize(800, 80)) == 7,
          "wide single-line region selects PSM 7");
    check(OcrImagePreprocessor::selectPageSegmentationMode(QSize(520, 90)) == 7,
          "tested single-line subtitle region selects PSM 7");
    check(OcrImagePreprocessor::selectPageSegmentationMode(QSize(800, 180)) == 6,
          "taller multi-line region selects PSM 6");

    QTemporaryDir noLanguageData;
    check(noLanguageData.isValid(), "temporary empty tessdata directory exists");
    TesseractOcrEngine engine(noLanguageData.path());
    const OcrResult missing = engine.recognize(source, QStringLiteral("zh"));
    check(missing.error.contains(QStringLiteral("Missing Tesseract language data: chi_sim")),
          "missing Chinese traineddata produces a specific error");

    if (failures == 0) {
        std::cout << "All Phase 4.1 OCR tuning checks passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
