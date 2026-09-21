#pragma once

#include <QImage>
#include <QMetaType>
#include <QRect>
#include <QString>

class QScreen;

struct CaptureResult
{
    QImage image;
    QRect globalRect;
    QString screenName;
    qreal devicePixelRatio = 1.0;
    QString error;

    bool isValid() const;
};

Q_DECLARE_METATYPE(CaptureResult)

class ScreenCaptureService final
{
public:
    CaptureResult capture(QScreen *screen, const QRect &globalLogicalRect) const;

    static QRect toScreenLocalRect(const QRect &globalLogicalRect,
                                   const QRect &screenGeometry);
    static bool isRegionValid(const QRect &globalLogicalRect,
                              const QRect &screenGeometry,
                              int minimumSize = 10);
};
