#include "app/OverlayInteractionController.h"
#include "gui/TranslationWindow.h"
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
}

void OverlayInteractionController::initialize()
{
    shortcuts_.registerDefaults();
    const bool safe = shortcuts_.isRegistered(GlobalShortcutManager::Action::ToggleInteraction);
    window_.setInteractionMode(safe && settings_.overlayClickThrough()
        ? OverlayInteractionMode::ClickThrough : OverlayInteractionMode::Interactive);
}

void OverlayInteractionController::routeAction(GlobalShortcutManager::Action action)
{
    // Keep the entry mode unchanged throughout selection and its capture delay.
    if (actions_.selectingRegion()) return;
    switch (action) {
    case GlobalShortcutManager::Action::ToggleInteraction:
        if (!shortcuts_.isRegistered(action)) return;
        window_.setInteractionMode(window_.interactionMode() == OverlayInteractionMode::Interactive
            ? OverlayInteractionMode::ClickThrough : OverlayInteractionMode::Interactive);
        settings_.setOverlayClickThrough(window_.interactionMode() == OverlayInteractionMode::ClickThrough);
        break;
    case GlobalShortcutManager::Action::SelectRegion:
        actions_.selectRegion();
        break;
    case GlobalShortcutManager::Action::ToggleRealtime:
        if (actions_.realtimeRunning()) actions_.stop();
        else actions_.start();
        break;
    }
}
