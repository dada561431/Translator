#include "capture/ScreenCaptureService.h"

#include <QPixmap>
#include <QScreen>

bool CaptureResult::isValid() const
{
    return error.isEmpty() && !image.isNull() && globalRect.isValid()
        && !screenName.isEmpty();
}

CaptureResult ScreenCaptureService::capture(QScreen *screen,
                                            const QRect &globalLogicalRect) const
{
    CaptureResult result;
    result.globalRect = globalLogicalRect;

    if (!screen) {
        result.error = QStringLiteral("Capture screen is unavailable.");
        return result;
    }

    result.screenName = screen->name();
    result.devicePixelRatio = screen->devicePixelRatio();
    if (!isRegionValid(globalLogicalRect, screen->geometry())) {
        result.error = QStringLiteral("Capture region is invalid or outside the selected screen.");
        return result;
    }

    const QRect localRect = toScreenLocalRect(globalLogicalRect, screen->geometry());
    const QPixmap pixmap = screen->grabWindow(
        0, localRect.x(), localRect.y(), localRect.width(), localRect.height());
    if (pixmap.isNull()) {
        result.error = QStringLiteral("QScreen::grabWindow returned an empty pixmap.");
        return result;
    }

    result.image = pixmap.toImage();
    if (result.image.isNull()) {
        result.error = QStringLiteral("Captured pixmap could not be converted to QImage.");
    }
    return result;
}

QRect ScreenCaptureService::toScreenLocalRect(const QRect &globalLogicalRect,
                                              const QRect &screenGeometry)
{
    return globalLogicalRect.translated(-screenGeometry.topLeft());
}

bool ScreenCaptureService::isRegionValid(const QRect &globalLogicalRect,
                                         const QRect &screenGeometry,
                                         int minimumSize)
{
    return globalLogicalRect.isValid()
        && globalLogicalRect.width() >= minimumSize
        && globalLogicalRect.height() >= minimumSize
        && screenGeometry.contains(globalLogicalRect);
}
