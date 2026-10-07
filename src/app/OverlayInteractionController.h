#pragma once
#include <QObject>
#include <functional>
#include "app/GlobalShortcutManager.h"
#include "gui/OverlayControlAction.h"

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
        std::function<void()> exit;
        std::function<bool()> regionAvailable;
    };
    OverlayInteractionController(TranslationWindow &window, SettingsManager &settings,
                                 GlobalShortcutManager &shortcuts, Actions actions, QObject *parent = nullptr);
    void initialize();
    ~OverlayInteractionController() override;
    void routeAction(GlobalShortcutManager::Action action);
    void routeControl(OverlayControlAction action);
    void setTrayAvailable(bool available) { trayAvailable_ = available; }
    bool running() const { return actions_.realtimeRunning(); }
    bool selecting() const { return actions_.selectingRegion(); }
    bool regionAvailable() const { return !actions_.regionAvailable || actions_.regionAvailable(); }
    bool canClickThrough() const;
    void refreshState() { emit stateChanged(); }

signals:
    void stateChanged();

private:
    TranslationWindow &window_;
    SettingsManager &settings_;
    GlobalShortcutManager &shortcuts_;
    Actions actions_;
    bool trayAvailable_ = false;
    bool exiting_ = false;
};
