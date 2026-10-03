#pragma once
#include <QImage>

class FrameComparator final
{
public:
    static constexpr int PixelTolerance = 18;
    static constexpr double ChangedRatio = 0.0005;
    static constexpr double MeanTolerance = 2.0;
    bool accept(const QImage &image);
    void reset();
    static QImage thumbnail(const QImage &image);
private:
    QImage previous_;
    QSize inputSize_;
};
