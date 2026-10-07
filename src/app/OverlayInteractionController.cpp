#include "app/OverlayInteractionController.h"
#include "gui/TranslationWindow.h"
#include "gui/SettingsDialog.h"
#include "config/SettingsManager.h"
#include <utility>

OverlayInteractionController::OverlayInteractionController(
    TranslationWindow &window, SettingsManager &settings, GlobalShortcutManager &shortcuts,
    Actions actions, QObject *parent)
    : QObject(parent), window_(window), settings_(settings), shortcuts_(shortcuts), actions_(std::move(actions))
{
    connect(&shortcuts_, &GlobalShortcutManager::activated, this, &OverlayInteractionController::routeAction);
    connect(&shortcuts_, &GlobalShortcutManager::registrationFailed, &window_,
            [&window](GlobalShortcutManager::Action, const QString &message) { window.setRegionFeedback(message); });
    window_.setControlRouter([this](OverlayControlAction action) { routeControl(action); });
    window_.settingsDialog()->setHotkeyApplyHandler([this](const GlobalHotkeyConfig &config, QString &error) {
        if (selecting()) { error = QStringLiteral("Finish region selection before applying shortcuts."); return false; }
        if (config.shortcuts == settings_.globalHotkeys().shortcuts
            && config.shortcuts == shortcuts_.configuration().shortcuts) return true;
        if (!shortcuts_.replace(config, error, [this](const GlobalHotkeyConfig &value) { settings_.setGlobalHotkeys(value); })) return false;
        emit stateChanged(); return true;
    });
    connect(&window_, &TranslationWindow::interactionModeChanged, this, &OverlayInteractionController::stateChanged);
    connect(&window_, &TranslationWindow::dragLockedChanged, this, &OverlayInteractionController::stateChanged);
    connect(&window_, &TranslationWindow::overlayVisibilityChanged, this, &OverlayInteractionController::stateChanged);
    connect(&window_, &TranslationWindow::closed, this, [this] { routeControl(OverlayControlAction::Exit); });
}
OverlayInteractionController::~OverlayInteractionController()
{
    window_.setControlRouter({});
    window_.settingsDialog()->setHotkeyApplyHandler({});
}
bool OverlayInteractionController::canClickThrough() const
{
    return trayAvailable_ || shortcuts_.isRegistered(GlobalShortcutManager::Action::ToggleInteraction);
}
void OverlayInteractionController::initialize()
{
    shortcuts_.registerConfigured(settings_.globalHotkeys());
    const bool safe = shortcuts_.isRegistered(GlobalShortcutManager::Action::ToggleInteraction);
    window_.setInteractionMode(safe && settings_.overlayClickThrough()
        ? OverlayInteractionMode::ClickThrough : OverlayInteractionMode::Interactive);
    emit stateChanged();
}
void OverlayInteractionController::routeAction(GlobalShortcutManager::Action action)
{
    if (!shortcuts_.isRegistered(action)) return;
    switch (action) {
    case GlobalShortcutManager::Action::ToggleInteraction: routeControl(OverlayControlAction::ToggleInteraction); break;
    case GlobalShortcutManager::Action::SelectRegion: routeControl(OverlayControlAction::SelectRegion); break;
    case GlobalShortcutManager::Action::ToggleRealtime: routeControl(OverlayControlAction::ToggleRealtime); break;
    }
}
void OverlayInteractionController::routeControl(OverlayControlAction action)
{
    if (action == OverlayControlAction::Exit) {
        if (exiting_) return;
        exiting_ = true;
        actions_.stop();
        window_.close();
        if (actions_.exit) actions_.exit();
        return;
    }
    if (selecting()) return;
    switch (action) {
    case OverlayControlAction::ToggleInteraction:
        if (window_.interactionMode() == OverlayInteractionMode::Interactive && !canClickThrough()) return;
        window_.setInteractionMode(window_.interactionMode() == OverlayInteractionMode::Interactive
            ? OverlayInteractionMode::ClickThrough : OverlayInteractionMode::Interactive);
        settings_.setOverlayClickThrough(window_.interactionMode() == OverlayInteractionMode::ClickThrough);
        break;
    case OverlayControlAction::RestoreInteractive:
        window_.setInteractionMode(OverlayInteractionMode::Interactive);
        settings_.setOverlayClickThrough(false);
        window_.show(); break;
    case OverlayControlAction::SelectRegion: if (regionAvailable()) actions_.selectRegion(); break;
    case OverlayControlAction::ToggleRealtime: if (running()) actions_.stop(); else actions_.start(); break;
    case OverlayControlAction::Start: if (!running()) actions_.start(); break;
    case OverlayControlAction::Stop: actions_.stop(); break;
    case OverlayControlAction::ToggleLock: settings_.setOverlayDragLocked(!window_.dragLocked()); break;
    case OverlayControlAction::ShowOverlay: window_.show(); break;
    case OverlayControlAction::HideOverlay:
        // Never hide the last recovery surface when the notification area is unavailable.
        if (trayAvailable_) window_.hide(); break;
    case OverlayControlAction::OpenSettings: window_.openSettings(); break;
    case OverlayControlAction::Exit: break;
    }
    emit stateChanged();
}
