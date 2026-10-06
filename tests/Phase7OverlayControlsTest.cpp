#include "app/GlobalShortcutManager.h"
#include "app/OverlayInteractionController.h"
#include "app/CaptureCoordinator.h"
#include "gui/OverlayTrayController.h"
#include "gui/TranslationWindow.h"
#include "gui/SettingsDialog.h"
#include "config/SettingsManager.h"
#include "capture/RegionSelector.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QTemporaryDir>
#include <QThread>
#include <QSemaphore>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVBoxLayout>
#include <QSystemTrayIcon>
#include <thread>
#include <map>
#include <iostream>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
int failures = 0;
void check(bool value, const char *message) { if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }
void wait(int ms) {
    QElapsedTimer clock; clock.start();
    while (clock.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(2); }
}
struct FakeRegistry {
    std::map<int, GlobalHotkeyChord> owned;
    unsigned failKey = 0;
    unsigned failModifiers = 0;
    int registrations = 0, releases = 0;
    GlobalShortcutManager::Backend backend() {
        return {{}, [this](int id) { check(owned.erase(id) == 1, "unregister exactly owned success"); ++releases; },
            [this](int id, unsigned modifiers, unsigned key, QString &error) {
                ++registrations;
                if (key == failKey && (!failModifiers || failModifiers == modifiers)) { error = "fixture conflict 1409"; return false; }
                for (const auto &item : owned) if (item.second == GlobalHotkeyChord{modifiers, key}) {
                    error = "self conflict"; return false;
                }
                check(!owned.count(id), "staged ID never aliases an old registration");
                owned[id] = {modifiers, key}; return true;
            }};
    }
};
GlobalHotkeyConfig custom() { return {{{"Ctrl+Shift+T", "Ctrl+Shift+R", "Ctrl+Shift+S"}}}; }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setOrganizationName("TranslatorPhase7B2Tests"); app.setApplicationName("Controls");
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    SettingsManager settings;
    if (app.arguments().contains("--desktop-probe")) {
        settings.setOverlayExcludeFromCapture(false); // QA-only visibility; never touches production preferences.
        TranslationWindow overlay(settings);
        overlay.setWindowTitle("Translator Controls QA");
        CaptureCoordinator capture(overlay, settings, nullptr, false);
        GlobalShortcutManager native;
        bool active = false;
        int starts = 0, stops = 0, regions = 0, toggles = 0;
        QWidget foreground;
        foreground.setWindowTitle("Translator Foreground QA Fixture");
        auto *layout = new QVBoxLayout(&foreground);
        auto *label = new QLabel("Foreground fixture", &foreground); layout->addWidget(label);
        auto *menuButton = new QPushButton("Inspect tray action menu", &foreground); layout->addWidget(menuButton);
        OverlayInteractionController controller(overlay, settings, native, {
            [&] { return capture.isSelecting(); }, [&] { ++regions; capture.beginSelection(); }, [&] { return active; },
            [&] { ++starts; active = true; overlay.setTranslationRunning(true); },
            [&] { ++stops; active = false; overlay.setTranslationRunning(false); }, [&] { app.quit(); }
        });
        OverlayTrayController tray(controller, overlay);
        QObject::connect(menuButton, &QPushButton::clicked, &foreground, [&] { tray.menu()->popup(foreground.mapToGlobal(QPoint(20, 80))); });
        QObject::connect(&overlay, &TranslationWindow::interactionModeChanged, &foreground, [&] {
            ++toggles; label->setText(overlay.interactionMode() == OverlayInteractionMode::ClickThrough ? "ClickThrough" : "Interactive");
        });
        QObject::connect(&capture, &CaptureCoordinator::selectionStarted, &controller, &OverlayInteractionController::refreshState);
        QObject::connect(&capture, &CaptureCoordinator::selectionFinished, &controller, &OverlayInteractionController::refreshState);
        overlay.show(); controller.initialize();
        foreground.resize(400, 160); foreground.move(50, 60); foreground.show();
        const QRect initial = overlay.geometry();
        const int argument = app.arguments().indexOf("--desktop-probe");
        const QString report = app.arguments().value(argument + 1);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] {
            const auto hotkeys = settings.globalHotkeys();
            const QJsonObject data{{"tray_available", tray.available()}, {"toggles", toggles}, {"starts", starts},
                {"stops", stops}, {"regions", regions}, {"geometry_changed", overlay.geometry() != initial},
                {"locked", overlay.dragLocked()}, {"visible", overlay.isVisible()},
                {"hotkeys", QStringList(hotkeys.shortcuts.begin(), hotkeys.shortcuts.end()).join(";")}};
            QFile file(report); if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(data).toJson());
            native.unregisterAll(); tray.shutdown();
        });
        QTimer::singleShot(120000, &app, &QCoreApplication::quit);
        return app.exec();
    }
#ifdef Q_OS_WIN
    if (app.arguments().contains("--native-registration") || app.arguments().contains("--native-registration-isolated")) {
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
            std::cerr << message.toStdString() << '\n';
        });
        GlobalHotkeyConfig nativeBaseline;
        if (app.arguments().contains("--native-registration-isolated"))
            nativeBaseline.shortcuts = {{"Ctrl+Alt+F9", "Ctrl+Alt+F10", "Ctrl+Alt+F11"}};
        settings.setGlobalHotkeys(nativeBaseline);
        TranslationWindow nativeWindow(settings);
        GlobalShortcutManager native;
        bool active = false;
        OverlayInteractionController controller(nativeWindow, settings, native, {
            [] { return false; }, [] {}, [&] { return active; }, [&] { active = true; }, [&] { active = false; }
        });
        controller.initialize();
        const int oldToggle = native.registeredId(GlobalShortcutManager::Action::ToggleInteraction);
        for (int i = 0; i < 3; ++i) {
            std::cout << nativeBaseline.shortcuts[i].toStdString() << " registered=" << native.isRegistered(GlobalShortcutManager::Action(i)) << '\n';
            check(native.isRegistered(GlobalShortcutManager::Action(i)), "native baseline registered");
        }
        if (failures) return 1; // Do not displace another application's registered shortcuts.
        QSemaphore ready, release;
        bool occupied = false;
        std::thread fixture([&] {
            occupied = RegisterHotKey(nullptr, 0x5111, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'R');
            ready.release(); release.acquire();
            if (occupied) UnregisterHotKey(nullptr, 0x5111);
        });
        ready.acquire();
        check(occupied, "controlled other-thread fixture owns Ctrl+Shift+R");
        auto *dialog = nativeWindow.settingsDialog(); dialog->show();
        const QStringList names{"toggleInteractionHotkey", "regionHotkey", "startStopHotkey"};
        for (int i = 0; i < 3; ++i) dialog->findChild<QKeySequenceEdit *>(names[i])->setKeySequence(QKeySequence(custom().shortcuts[i]));
        auto *apply = dialog->findChild<QDialogButtonBox *>("settingsButtonBox")->button(QDialogButtonBox::Apply);
        apply->click();
        check(settings.globalHotkeys().shortcuts == nativeBaseline.shortcuts, "native conflict Apply leaves persisted baseline intact");
        check(!dialog->findChild<QLabel *>("credentialError")->text().isEmpty(), "native conflict reported inline");
        check(native.registeredId(GlobalShortcutManager::Action::ToggleInteraction) == oldToggle, "native conflict retains original recovery registration");
        bool rollbackReleasedNewToggle = false;
        std::thread rollbackCheck([&] {
            rollbackReleasedNewToggle = RegisterHotKey(nullptr, 0x5112, MOD_CONTROL | MOD_SHIFT, 'T');
            if (rollbackReleasedNewToggle) UnregisterHotKey(nullptr, 0x5112);
        });
        rollbackCheck.join();
        check(rollbackReleasedNewToggle, "native rollback releases staged Ctrl+Shift+T for another thread");
        release.release(); fixture.join();
        apply->click();
        check(settings.globalHotkeys().shortcuts == custom().shortcuts && dialog->findChild<QLabel *>("credentialError")->text().isEmpty(), "native Apply succeeds after fixture releases key");
        check(!native.dispatchHotkey(oldToggle), "obsolete native registration no longer dispatches");
        bool obsoleteReleased = false;
        std::thread releaseCheck([&] {
            const unsigned oldKey = app.arguments().contains("--native-registration-isolated") ? VK_F9 : 'T';
            obsoleteReleased = RegisterHotKey(nullptr, 0x5113, MOD_CONTROL | MOD_ALT, oldKey);
            if (obsoleteReleased) UnregisterHotKey(nullptr, 0x5113);
        });
        releaseCheck.join();
        check(obsoleteReleased, "old native recovery chord is actually released after successful Apply");
        dialog->close();
        native.unregisterAll();
        controller.initialize();
        check(native.configuration().shortcuts == custom().shortcuts, "native restart reads saved PortableText keys");
        native.unregisterAll();
        GlobalShortcutManager restarted;
        restarted.registerConfigured(custom());
        for (int i = 0; i < 3; ++i) check(restarted.isRegistered(GlobalShortcutManager::Action(i)), "native cleanup permits new manager to register");
        restarted.unregisterAll();
        std::cout << "Native RegisterHotKey / Settings conflict fixture: " << failures << " failures\n";
        return failures ? 1 : 0;
    }
#endif
    GlobalHotkeyConfig canonical;
    std::array<GlobalHotkeyChord, 3> chords;
    QString error;
    check(parseGlobalHotkeys({}, canonical, chords, error) && chords[0] == GlobalHotkeyChord{3, 'T'}, "defaults parse");
    for (const QString &invalid : {QString(), QString("T"), QString("Ctrl"), QString("Ctrl+Alt"), QString("Ctrl+T, Ctrl+R"), QString("garbage")}) {
        auto config = custom(); config.shortcuts[0] = invalid;
        check(!parseGlobalHotkeys(config, canonical, chords, error), "reject empty/bare/modifier/multichord/unknown");
    }
    auto duplicate = custom(); duplicate.shortcuts[1] = "Shift+Ctrl+T";
    check(!parseGlobalHotkeys(duplicate, canonical, chords, error), "duplicate normalized chords rejected");
    auto meta = custom(); meta.shortcuts[0] = "Meta+F12";
    check(parseGlobalHotkeys(meta, canonical, chords, error) && chords[0] == GlobalHotkeyChord{8, 0x7b}, "Win/function key mapping");
    check(!settings.overlayDragLocked(), "lock defaults off");

    FakeRegistry registry;
    GlobalShortcutManager shortcuts(nullptr, registry.backend());
    shortcuts.registerDefaults();
    const int originalToggle = shortcuts.registeredId(GlobalShortcutManager::Action::ToggleInteraction);
    const auto initialOwned = registry.owned;
    registry.failKey = 'R'; registry.failModifiers = 6;
    bool persisted = false;
    check(!shortcuts.replace(custom(), error, [&](const auto &) { persisted = true; }), "partial registration conflict fails transaction");
    check(!persisted && registry.owned == initialOwned && shortcuts.configuration().shortcuts == GlobalHotkeyConfig{}.shortcuts,
          "rollback preserves all old shortcuts and storage");
    check(registry.releases == 1 && shortcuts.dispatchHotkey(originalToggle), "only staged new registration removed; recovery alive");
    registry.failKey = 0;
    check(shortcuts.replace(custom(), error, [&](const auto &config) {
        check(registry.owned.size() == 6, "persist before obsolete old keys released");
        settings.setGlobalHotkeys(config);
    }), "successful replacement");
    check(registry.owned.size() == 3 && !shortcuts.dispatchHotkey(originalToggle), "old IDs released after success");
    check(SettingsManager().globalHotkeys().shortcuts == custom().shortcuts, "PortableText survives fresh settings instance");
    const int count = registry.registrations;
    check(shortcuts.replace(custom(), error) && registry.registrations == count, "unchanged replacement reuses keys");
    auto permutation = custom(); std::swap(permutation.shortcuts[0], permutation.shortcuts[1]);
    const int oldRegion = shortcuts.registeredId(GlobalShortcutManager::Action::SelectRegion);
    check(shortcuts.replace(permutation, error) && registry.registrations == count, "permutation without self conflict");
    int dispatched = -1;
    QObject::connect(&shortcuts, &GlobalShortcutManager::activated, &app, [&](auto action) { dispatched = int(action); });
    check(shortcuts.dispatchHotkey(oldRegion) && dispatched == 0, "permutation dispatch maps new action");
#ifdef Q_OS_WIN
    MSG msg{}; msg.message = WM_HOTKEY; msg.wParam = oldRegion; msg.lParam = MAKELPARAM(6, 'R');
    qintptr result = 42;
    check(shortcuts.nativeEventFilter("windows_dispatcher_MSG", &msg, &result) && result == 0, "native custom modifier/chord dispatch");
    msg.lParam = MAKELPARAM(3, 'R');
    check(!shortcuts.nativeEventFilter("windows_dispatcher_MSG", &msg, &result), "native stale/default chord rejected");
#endif
    shortcuts.unregisterAll(); shortcuts.unregisterAll();
    check(registry.owned.empty(), "idempotent cleanup");

    settings.setGlobalHotkeys({});
    int nativeMoves = 0, nativeResizes = 0;
    TranslationWindow window(settings, nullptr, nullptr, {}, {
        [&] { ++nativeMoves; return true; }, [&](Qt::Edges) { ++nativeResizes; return true; }
    });
    window.show(); app.processEvents();
    bool running = false, selecting = false;
    int starts = 0, stops = 0, regions = 0, exits = 0;
    OverlayInteractionController controller(window, settings, shortcuts, {
        [&] { return selecting; }, [&] { ++regions; }, [&] { return running; },
        [&] { running = true; ++starts; window.setTranslationRunning(true); },
        [&] { running = false; ++stops; window.setTranslationRunning(false); }, [&] { ++exits; }
    });
    controller.initialize();
    OverlayTrayController tray(controller, window, nullptr, [] { return true; }, false);
    auto action = [&](const char *name) { return tray.menu()->findChild<QAction *>(QString::fromLatin1(name)); };
    auto *lockButton = window.findChild<QPushButton *>("lockButton");
    lockButton->click();
    check(window.dragLocked() && settings.overlayDragLocked() && SettingsManager().overlayDragLocked(), "toolbar lock persisted");
    check(!window.geometryInteractionAllowed() && lockButton->isChecked() && lockButton->text() == "Unlock", "lock disables manipulation; button reflects state");
    auto *subtitle = window.findChild<QWidget *>("subtitleArea");
    QMouseEvent edgeMove(QEvent::MouseMove, QPointF(0, 0), QPointF(window.mapToGlobal(QPoint(0, 0))), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(subtitle, &edgeMove);
    check(window.cursor().shape() == Qt::ArrowCursor, "locked edge has no resize cursor");
    auto *toolbar = window.findChild<QWidget *>("toolbar");
    QMouseEvent toolbarPress(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(window.mapToGlobal(QPoint(10, 10))), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent edgePress(QEvent::MouseButtonPress, QPointF(0, 0), QPointF(window.mapToGlobal(QPoint(0, 0))), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(toolbar, &toolbarPress); QApplication::sendEvent(subtitle, &edgePress);
    check(nativeMoves == 0 && nativeResizes == 0, "locked events never invoke native move or resize");
    const QRect geometry = window.geometry();
    action("trayInteraction")->trigger();
    check(window.interactionMode() == OverlayInteractionMode::ClickThrough && window.dragLocked(), "mode preserves lock");
    check(!window.findChild<QWidget *>("toolbar")->isVisible(), "passthrough hides toolbar");
    action("trayLock")->trigger();
    check(!window.dragLocked() && !window.geometryInteractionAllowed(), "unlock does not make ClickThrough interactive");
    action("trayInteraction")->trigger();
    check(window.geometryInteractionAllowed() && window.geometry() == geometry, "Interactive restoration retains geometry");
    QApplication::sendEvent(toolbar, &toolbarPress); QApplication::sendEvent(subtitle, &edgePress);
    check(nativeMoves == 1 && nativeResizes == 1, "unlocked events request native manipulation via injectable boundary");
    window.findChild<QPushButton *>("startButton")->click();
    check(running && starts == 1 && action("trayRealtime")->text() == "Stop", "toolbar starts through same router and synchronizes tray");
    const int beforeHideStops = stops;
    action("trayVisibility")->trigger();
    check(!window.isVisible() && running && stops == beforeHideStops && action("trayVisibility")->text() == "Show Overlay", "hide never stops pipeline");
    window.setOriginalText("latest OCR while hidden"); window.setTranslatedText("latest translation while hidden");
    action("traySettings")->trigger(); app.processEvents();
    auto *dialog = window.settingsDialog();
    check(!window.isVisible() && dialog->isVisible() && !dialog->windowFlags().testFlag(Qt::WindowTransparentForInput), "hidden independent Settings remains interactive");
    auto *edit = dialog->findChild<QKeySequenceEdit *>("toggleInteractionHotkey");
    auto *buttons = dialog->findChild<QDialogButtonBox *>("settingsButtonBox");
    const int beforeCancel = registry.registrations;
    edit->setKeySequence(QKeySequence("Ctrl+Shift+T")); dialog->reject();
    check(registry.registrations == beforeCancel && settings.globalHotkeys().shortcuts == GlobalHotkeyConfig{}.shortcuts, "Cancel neither registers nor saves shortcuts");
    action("traySettings")->trigger();
    edit->setKeySequence(QKeySequence("Ctrl+T, Ctrl+R"));
    buttons->button(QDialogButtonBox::Apply)->click();
    check(!dialog->findChild<QLabel *>("credentialError")->text().isEmpty() && registry.registrations == beforeCancel, "Apply rejects multi-chord inline");
    const QStringList editNames{"toggleInteractionHotkey", "regionHotkey", "startStopHotkey"};
    for (int i = 0; i < 3; ++i) dialog->findChild<QKeySequenceEdit *>(editNames[i])->setKeySequence(QKeySequence(custom().shortcuts[i]));
    registry.failKey = 'S'; registry.failModifiers = 6;
    const auto oldConfig = settings.globalHotkeys();
    buttons->button(QDialogButtonBox::Apply)->click();
    check(settings.globalHotkeys().shortcuts == oldConfig.shortcuts && registry.owned.size() == 3, "Settings transaction failure preserves configuration and recovery");
    check(dialog->findChild<QLabel *>("credentialError")->text().contains("Previous shortcuts"), "conflict inline human-readable");
    registry.failKey = 0;
    buttons->button(QDialogButtonBox::Apply)->click();
    check(settings.globalHotkeys().shortcuts == custom().shortcuts && dialog->findChild<QLabel *>("credentialError")->text().isEmpty(), "Settings successful Apply saves registered custom chords");
    dialog->close();
    action("trayVisibility")->trigger();
    check(window.isVisible() && window.findChild<QLabel *>("originalLabel")->text() == "latest OCR while hidden"
        && window.findChild<QLabel *>("translatedLabel")->text() == "latest translation while hidden", "show displays latest hidden subtitles");
    action("trayRegion")->trigger(); check(regions == 1, "tray Region uses controller");
    action("trayRealtime")->trigger(); check(!running, "tray Stop uses controller");
    action("trayRealtime")->trigger(); check(running, "tray Start uses controller");
    selecting = true; controller.refreshState();
    check(!action("trayVisibility")->isEnabled() && action("trayExit")->isEnabled(), "selection disables control mutation but permits Exit");
    controller.routeControl(OverlayControlAction::HideOverlay);
    check(window.isVisible(), "selection guard preserves entry visibility");
    selecting = false; controller.refreshState();
    {
        CaptureCoordinator capture(window, settings, nullptr, false);
        window.hide(); capture.beginSelection(); wait(100);
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&window, &escape); wait(20);
        check(!capture.isSelecting() && !window.isVisible(), "hidden Region cancel remains hidden");
        capture.beginSelection(); wait(100);
        auto *selector = capture.findChild<RegionSelector *>();
        emit selector->regionSelected(QRect(), nullptr); wait(180);
        check(!capture.isSelecting() && !window.isVisible(), "invalid selected capture restores hidden entry visibility");
        QApplication::sendEvent(&window, &escape); wait(10);
        window.show(); capture.beginSelection(); wait(100);
        QApplication::sendEvent(&window, &escape); wait(20);
        check(!capture.isSelecting() && window.isVisible(), "visible Region cancel restores visible");
    }
    tray.shutdown();
    {
        OverlayTrayController unavailable(controller, window, nullptr, [] { return false; }, false);
        const auto owned = registry.owned;
        controller.routeControl(OverlayControlAction::HideOverlay);
        check(window.isVisible() && registry.owned == owned, "unavailable tray safe: cannot orphan hidden overlay");
    }
    // Startup conflicts and malformed storage must not erase preferences.
    settings.setOverlayClickThrough(true);
    settings.setGlobalHotkeys(custom());
    registry.failKey = 'T'; registry.failModifiers = 6;
    controller.initialize();
    check(shortcuts.isRegistered(GlobalShortcutManager::Action::ToggleInteraction)
          && shortcuts.configuration().shortcuts[0] == "Ctrl+Alt+T", "custom toggle conflict uses default recovery");
    check(settings.globalHotkeys().shortcuts == custom().shortcuts, "startup fallback leaves storage intact");
    registry.failModifiers = 0;
    controller.initialize();
    check(window.interactionMode() == OverlayInteractionMode::Interactive && settings.overlayClickThrough(), "all recovery failures force Interactive without erasing saved mode");
    window.openSettings();
    const int unchangedAttempts = registry.registrations;
    window.settingsDialog()->findChild<QDialogButtonBox *>("settingsButtonBox")->button(QDialogButtonBox::Apply)->click();
    check(registry.registrations == unchangedAttempts
          && window.settingsDialog()->findChild<QLabel *>("credentialError")->text().isEmpty()
          && window.interactionMode() == OverlayInteractionMode::Interactive,
          "unchanged Apply does not retry conflicted shortcuts or change safe startup mode");
    window.settingsDialog()->close();
    {
        OverlayTrayController recovery(controller, window, nullptr, [] { return true; }, false);
        controller.routeControl(OverlayControlAction::ToggleInteraction);
        check(window.interactionMode() == OverlayInteractionMode::ClickThrough, "tray provides independent recovery when toggle hotkey unavailable");
        controller.routeControl(OverlayControlAction::HideOverlay);
        emit recovery.findChild<QSystemTrayIcon *>()->activated(QSystemTrayIcon::DoubleClick);
        check(window.isVisible() && window.interactionMode() == OverlayInteractionMode::Interactive,
              "tray double-click shows overlay and restores Interactive without hotkey");
    }
    QSettings raw;
    raw.setValue("hotkeys/toggleInteraction", "bad stored value"); raw.sync();
    registry.failKey = 0;
    controller.initialize();
    check(shortcuts.configuration().shortcuts == GlobalHotkeyConfig{}.shortcuts, "malformed stored configuration uses defaults");
    raw.sync(); check(raw.value("hotkeys/toggleInteraction").toString() == "bad stored value", "malformed stored value not rewritten");
    tray.refresh();
    action("trayExit")->trigger();
    check(exits == 1 && !window.isVisible() && !running, "tray Exit follows normal close and stop path once");
    controller.routeControl(OverlayControlAction::Exit); check(exits == 1, "Exit idempotent");
    shortcuts.unregisterAll();
    check(registry.owned.empty(), "all owned shortcuts cleaned up");
    std::cout << "Phase 7B.2 controls: " << failures << " failures\n";
    return failures ? 1 : 0;
}
