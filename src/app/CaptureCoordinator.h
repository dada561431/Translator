#pragma once

#include <QObject>

#include "capture/RegionSelector.h"
#include "capture/ScreenCaptureService.h"

class SettingsManager;
class TranslationWindow;

class CaptureCoordinator final : public QObject
{
public:
    CaptureCoordinator(TranslationWindow &window, SettingsManager &settings,
                       QObject *parent = nullptr);

private:
    void beginSelection();
    void captureSelectedRegion(const QRect &region, QScreen *screen);
    void restoreWindow(const QString &feedback);

    TranslationWindow &window_;
    SettingsManager &settings_;
    RegionSelector selector_;
    ScreenCaptureService captureService_;
    bool selecting_ = false;
};
