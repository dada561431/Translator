#pragma once

#include <QImage>
#include <QString>

enum class OcrPreprocessMode
{
    Original,
    Upscale,
    UpscaleGrayscale,
    UpscaleGrayscaleContrast,
    Automatic,
};

struct OcrPreprocessOptions
{
    OcrPreprocessMode mode = OcrPreprocessMode::Automatic;
    int scaleFactor = 0;
};

struct OcrPreprocessResult
{
    QImage image;
    QString mode;
    int scaleFactor = 1;
    qint64 elapsedMs = 0;
};

class OcrImagePreprocessor final
{
public:
    static OcrPreprocessResult process(
        const QImage &source,
        const OcrPreprocessOptions &options = OcrPreprocessOptions());

    static int recommendedScale(const QSize &size);
    static int selectPageSegmentationMode(const QSize &inputSize);

private:
    static QImage enhanceContrast(const QImage &grayscale);
};
