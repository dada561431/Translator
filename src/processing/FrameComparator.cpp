#include "processing/FrameComparator.h"
#include <cstdlib>

QImage FrameComparator::thumbnail(const QImage &image)
{
    if (image.isNull()) return {};
    return image.scaled(QSize(160, 90), Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_Grayscale8);
}

void FrameComparator::reset()
{
    previous_ = {};
    inputSize_ = {};
}

bool FrameComparator::accept(const QImage &image)
{
    if (image.isNull()) return false;
    const QImage current = thumbnail(image);
    bool changed = previous_.isNull() || inputSize_ != image.size()
        || previous_.size() != current.size();
    if (!changed) {
        qint64 sum = 0, different = 0;
        const int count = current.width() * current.height();
        for (int y = 0; y < current.height(); ++y) {
            const auto *a = current.constScanLine(y);
            const auto *b = previous_.constScanLine(y);
            for (int x = 0; x < current.width(); ++x) {
                const int delta = std::abs(int(a[x]) - int(b[x]));
                sum += delta;
                if (delta >= PixelTolerance) ++different;
            }
        }
        changed = double(sum) / count >= MeanTolerance
            || double(different) / count >= ChangedRatio;
    }
    // Compare against the last accepted image, so gradual changes accumulate.
    if (changed) {
        previous_ = current;
        inputSize_ = image.size();
    }
    return changed;
}
