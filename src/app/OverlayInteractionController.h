#pragma once
#include <QObject>
#include <functional>
#include "app/GlobalShortcutManager.h"

class TranslationWindow;
class SettingsManager;

class OverlayInteractionController final : public QObject
{
    Q_OBJECT
public:
    struct Actions {
        std::function<bool()> selectingRegion;
        std::function<void()> selectRegion;
        std::function<bool()> realtimeRunning;
        std::function<void()> start;
        std::function<void()> stop;
    };
    OverlayInteractionController(TranslationWindow &window, SettingsManager &settings,
                                 GlobalShortcutManager &shortcuts, Actions actions, QObject *parent = nullptr);
    void initialize();
    void routeAction(GlobalShortcutManager::Action action);

private:
    TranslationWindow &window_;
    SettingsManager &settings_;
    GlobalShortcutManager &shortcuts_;
    Actions actions_;
};
