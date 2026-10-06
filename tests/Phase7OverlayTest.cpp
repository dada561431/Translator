#include "app/GlobalShortcutManager.h"
#include "app/OverlayInteractionController.h"
#include "app/CaptureCoordinator.h"
#include "config/SettingsManager.h"
#include "gui/TranslationWindow.h"
#include "gui/SettingsDialog.h"
#include <QApplication>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
int failures = 0;
void check(bool value, const char *message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void wait(int ms) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(2); }
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setOrganizationName(QStringLiteral("TranslatorPhase7Tests"));
    app.setApplicationName(QStringLiteral("OverlayInteraction"));
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    SettingsManager settings;
    check(!settings.overlayClickThrough(), "first-run preference defaults Interactive");
    TranslationWindow window(settings);
    window.show();
    app.processEvents();
    auto *toolbar = window.findChild<QWidget *>(QStringLiteral("toolbar"));
    auto *original = window.findChild<QLabel *>(QStringLiteral("originalLabel"));
    auto *translated = window.findChild<QLabel *>(QStringLiteral("translatedLabel"));
    int registered = 0, unregistered = 0;
    bool toggleAvailable = true;
    GlobalShortcutManager shortcuts(nullptr, {
        [&](int id, unsigned, QString &error) {
            ++registered;
            if (!toggleAvailable && id == GlobalShortcutManager::hotkeyId(GlobalShortcutManager::Action::ToggleInteraction)) {
                error = QStringLiteral("test conflict"); return false;
            }
            return true;
        }, [&](int) { ++unregistered; }
    });
    bool selecting = false, running = false;
    int regions = 0, starts = 0, stops = 0;
    OverlayInteractionController controller(window, settings, shortcuts, {
        [&] { return selecting; }, [&] { ++regions; }, [&] { return running; },
        [&] { ++starts; running = true; }, [&] { ++stops; running = false; }
    });
    controller.initialize();
    check(registered == 3, "three shortcuts registered");
    check(!shortcuts.dispatchHotkey(12345), "foreign ID not consumed");
    window.move(40, 50);
    window.resize(760, 190);
    window.setOriginalText(QStringLiteral("real OCR text"));
    window.setTranslatedText(QStringLiteral("real translated text"));
    app.processEvents();
    const QRect geometry = window.geometry();
    const auto toggle = GlobalShortcutManager::hotkeyId(GlobalShortcutManager::Action::ToggleInteraction);
    for (int i = 0; i < 20; ++i) {
        check(shortcuts.dispatchHotkey(toggle), "owned registered hotkey dispatched");
        app.processEvents();
        const bool through = i % 2 == 0;
        check((window.interactionMode() == OverlayInteractionMode::ClickThrough) == through, "explicit mode toggles");
        check(window.windowFlags().testFlag(Qt::WindowTransparentForInput) == through, "Qt input flag matches mode");
        check(window.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus) == through, "focus flag matches mode");
        check(window.windowFlags().testFlag(Qt::FramelessWindowHint)
              && window.windowFlags().testFlag(Qt::WindowStaysOnTopHint), "frameless and always-on-top retained");
        check(window.isVisible() && window.geometry() == geometry, "20 toggles retain visibility and geometry");
        check(original->text() == QStringLiteral("real OCR text")
              && translated->text() == QStringLiteral("real translated text"), "subtitles unchanged by toggling");
        if (through) {
            QEnterEvent enter(QPointF(20,20), QPointF(20,20), QPointF(window.mapToGlobal(QPoint(20,20))));
            QApplication::sendEvent(&window, &enter);
            QApplication::sendEvent(toolbar, &enter);
            check(!toolbar->isVisible(), "hover cannot reveal toolbar during passthrough");
        }
    }
    shortcuts.dispatchHotkey(toggle);
    wait(1100);
    check(!toolbar->isVisible(), "old timers cannot reveal click-through toolbar");
    check(!window.findChild<QLabel *>(QStringLiteral("interactionFeedback"))->isVisible(), "mode feedback expires");
    window.setOriginalText(QStringLiteral("updated during passthrough"));
    window.setTranslatedText(QStringLiteral("translation during passthrough"));
    check(original->text() == QStringLiteral("updated during passthrough")
          && translated->text() == QStringLiteral("translation during passthrough"), "content updates independent of input mode");
    const auto region = GlobalShortcutManager::hotkeyId(GlobalShortcutManager::Action::SelectRegion);
    const auto realtime = GlobalShortcutManager::hotkeyId(GlobalShortcutManager::Action::ToggleRealtime);
    shortcuts.dispatchHotkey(region);
    shortcuts.dispatchHotkey(realtime);
    shortcuts.dispatchHotkey(realtime);
    check(regions == 1 && starts == 1 && stops == 1 && !running, "region and Start/Stop route existing-state callbacks");
    check(window.interactionMode() == OverlayInteractionMode::ClickThrough, "Start/Stop independent from overlay mode");
    selecting = true;
    shortcuts.dispatchHotkey(toggle);
    shortcuts.dispatchHotkey(region);
    shortcuts.dispatchHotkey(realtime);
    check(regions == 1 && starts == 1 && window.interactionMode() == OverlayInteractionMode::ClickThrough,
          "selection guards mode and lifecycle through capture delay");
    selecting = false;
    {
        CaptureCoordinator capture(window, settings);
        emit window.regionSelectionRequested();
        wait(100);
        check(capture.isSelecting() && !window.isVisible(), "real coordinator hides overlay for selection");
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&window, &escape);
        wait(20);
        check(window.isVisible() && !capture.isSelecting()
              && window.interactionMode() == OverlayInteractionMode::ClickThrough, "cancel restores entry mode and window");
    }
    shortcuts.dispatchHotkey(toggle);
    window.findChild<QPushButton *>(QStringLiteral("settingsButton"))->click();
    app.processEvents();
    auto *dialog = window.findChild<QDialog *>(QStringLiteral("settingsDialog"));
    check(dialog->isVisible() && !dialog->windowFlags().testFlag(Qt::WindowTransparentForInput), "Settings interactive");
    shortcuts.dispatchHotkey(toggle);
    app.processEvents();
    check(dialog->isVisible() && !dialog->windowFlags().testFlag(Qt::WindowTransparentForInput), "Settings not made transparent by parent toggle");
    dialog->close();
    check(SettingsManager().overlayClickThrough(), "last user mode persists");
    toggleAvailable = false;
    controller.initialize();
    check(window.interactionMode() == OverlayInteractionMode::Interactive, "failed toggle forces safe runtime fallback");
    check(settings.overlayClickThrough(), "fallback does not erase saved preference");
    check(!shortcuts.dispatchHotkey(toggle), "unregistered toggle not dispatched");
    check(unregistered == 3 && registered == 6, "reinitialization unregisters before registering");
#ifdef Q_OS_WIN
    MSG message{};
    message.message = WM_MOUSEMOVE;
    qintptr result = 42;
    check(!shortcuts.nativeEventFilter("windows_generic_MSG", &message, &result) && result == 42, "foreign native event untouched");
    message.message = WM_HOTKEY;
    message.wParam = region;
    message.lParam = MAKELPARAM(MOD_CONTROL | MOD_ALT, 'R');
    check(shortcuts.nativeEventFilter("windows_dispatcher_MSG", &message, &result) && result == 0, "native registered message routes");
    message.lParam = MAKELPARAM(MOD_CONTROL, 'R');
    check(!shortcuts.nativeEventFilter("windows_dispatcher_MSG", &message, &result), "wrong hotkey chord untouched");
#endif
    shortcuts.unregisterAll();
    shortcuts.unregisterAll();
    check(unregistered == 5 && !shortcuts.dispatchHotkey(region), "cleanup idempotent and only unregisters owned successes");
    window.close();
    std::cout << "Phase 7A state/routing checks: " << failures << " failures\n";
    return failures ? 1 : 0;
}
