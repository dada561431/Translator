#include "ocr/OcrImagePreprocessor.h"

#include <QElapsedTimer>

#include <array>

namespace {

QString modeName(OcrPreprocessMode mode, int scaleFactor)
{
    switch (mode) {
    case OcrPreprocessMode::Original:
        return QStringLiteral("original");
    case OcrPreprocessMode::Upscale:
        return QStringLiteral("upscale%1x").arg(scaleFactor);
    case OcrPreprocessMode::UpscaleGrayscale:
        return QStringLiteral("upscale%1x+grayscale").arg(scaleFactor);
    case OcrPreprocessMode::UpscaleGrayscaleContrast:
    case OcrPreprocessMode::Automatic:
        return scaleFactor > 1
            ? QStringLiteral("upscale%1x+grayscale+contrast").arg(scaleFactor)
            : QStringLiteral("grayscale+contrast");
    }
    return QStringLiteral("unknown");
}

} // namespace

OcrPreprocessResult OcrImagePreprocessor::process(
    const QImage &source, const OcrPreprocessOptions &options)
{
    QElapsedTimer timer;
    timer.start();

    OcrPreprocessResult result;
    if (source.isNull()) {
        result.mode = QStringLiteral("invalid");
        result.elapsedMs = timer.elapsed();
        return result;
    }

    OcrPreprocessMode mode = options.mode;
    int scaleFactor = options.scaleFactor;
    if (mode == OcrPreprocessMode::Original) {
        scaleFactor = 1;
    } else if (scaleFactor <= 0) {
        scaleFactor = recommendedScale(source.size());
    }
    scaleFactor = qBound(1, scaleFactor, 3);

    QImage processed = source;
    if (mode != OcrPreprocessMode::Original && scaleFactor > 1) {
        processed = processed.scaled(source.size() * scaleFactor,
                                     Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
    }

    if (mode == OcrPreprocessMode::UpscaleGrayscale
        || mode == OcrPreprocessMode::UpscaleGrayscaleContrast
        || mode == OcrPreprocessMode::Automatic) {
        processed = processed.convertToFormat(QImage::Format_Grayscale8);
    }
    if (mode == OcrPreprocessMode::UpscaleGrayscaleContrast
        || mode == OcrPreprocessMode::Automatic) {
        processed = enhanceContrast(processed);
    }

    result.image = processed;
    result.scaleFactor = scaleFactor;
    result.mode = modeName(mode, scaleFactor);
    result.elapsedMs = timer.elapsed();
    return result;
}

int OcrImagePreprocessor::recommendedScale(const QSize &size)
{
    if (!size.isValid()) {
        return 1;
    }
    if (size.height() < 45) {
        return 3;
    }
    if (size.height() < 120) {
        return 2;
    }
    return 1;
}

int OcrImagePreprocessor::selectPageSegmentationMode(const QSize &inputSize)
{
    if (!inputSize.isValid()) {
        return 6;
    }
    const qreal aspectRatio = qreal(inputSize.width()) / inputSize.height();
    return aspectRatio >= 5.0 && inputSize.height() <= 110 ? 7 : 6;
}

QImage OcrImagePreprocessor::enhanceContrast(const QImage &grayscale)
{
    QImage output = grayscale.convertToFormat(QImage::Format_Grayscale8);
    if (output.isNull()) {
        return output;
    }

    std::array<quint64, 256> histogram{};
    quint64 pixelCount = 0;
    for (int y = 0; y < output.height(); ++y) {
        const uchar *line = output.constScanLine(y);
        for (int x = 0; x < output.width(); ++x) {
            ++histogram[line[x]];
            ++pixelCount;
        }
    }

    const quint64 tail = pixelCount / 100;
    quint64 accumulated = 0;
    int low = 0;
    for (; low < 255; ++low) {
        accumulated += histogram[low];
        if (accumulated > tail) {
            break;
        }
    }
    accumulated = 0;
    int high = 255;
    for (; high > 0; --high) {
        accumulated += histogram[high];
        if (accumulated > tail) {
            break;
        }
    }

    if (high - low < 32) {
        return output;
    }

    for (int y = 0; y < output.height(); ++y) {
        uchar *line = output.scanLine(y);
        for (int x = 0; x < output.width(); ++x) {
            line[x] = uchar(qBound(0, (int(line[x]) - low) * 255 / (high - low), 255));
        }
    }
    return output;
}
