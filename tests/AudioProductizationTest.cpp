#include "app/ProductionInputController.h"
#include "gui/TranslationWindow.h"
#include "gui/SettingsDialog.h"
#include "gui/OverlayTrayController.h"
#include "app/OverlayInteractionController.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMenu>
#include <QAction>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <iostream>

namespace {
int failures = 0;
void check(bool value, const char *label) { if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; } }
bool wait(std::function<bool()> predicate) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 2000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    return predicate();
}
class Backend final : public IAsrBackend {
public:
    std::atomic_int &loads;
    explicit Backend(std::atomic_int &count) : loads(count) {}
    Asr::Error loadModel(const QString &, const Asr::CancelToken &) override { ++loads; return {}; }
    void unloadModel() override {}
    Asr::BackendResult recognize(const std::vector<float> &, const Asr::Options &, const Asr::CancelToken &) override { return {}; }
};
class Input final : public IAudioInput {
public:
    Audio::InputKind kind; bool active = false;
    explicit Input(Audio::InputKind value) : kind(value) {}
    QList<Audio::DeviceInfo> devices() const override { return {{"stable-id", "Test device", kind, true}}; }
    void start(const QByteArray &) override { active = true; emit started(); }
    void stop() override { if (active) { active = false; emit stopped(); } }
    bool running() const override { return active; }
    Audio::NativeFormat nativeFormat() const override { return {16000, 1, Audio::SampleType::Int16}; }
    Audio::DeviceInfo selectedDevice() const override { return devices().front(); }
};
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv); QTemporaryDir temp;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    app.setOrganizationName("Phase8DTest"); app.setApplicationName("Isolated");
    SettingsManager settings;
    check(settings.inputMode() == "screen", "old users default screen");
    const auto model = temp.filePath("model.bin"); QFile file(model); check(file.open(QIODevice::WriteOnly), "fixture file"); file.write("fixture"); file.close();
    check(ProductionInputController::modelPath(model, temp.path()) == model, "explicit model");
    QDir(temp.path()).mkpath("models"); QFile::copy(model, temp.filePath("models/ggml-base.bin"));
    check(ProductionInputController::modelPath("missing", temp.path()) == temp.filePath("models/ggml-base.bin"), "stale path app-local fallback");
    check(ProductionInputController::modelPath("missing", temp.filePath("absent")).isEmpty(), "missing model");
    settings.setAudioSettings({"stable-id", "stable-id", model, "en"}); settings.setInputMode("microphone");
    { SettingsManager restored; check(restored.inputMode() == "microphone" && restored.audioSettings() == settings.audioSettings(), "settings roundtrip"); }
    std::atomic_int loads{0}; int captures = 0;
    AudioInputCoordinator capture(nullptr, [&](auto kind) { ++captures; return std::make_unique<Input>(kind); });
    AsrCoordinator asr([&] { return std::make_unique<Backend>(loads); });
    TranslationCoordinator translation(settings, std::unique_ptr<ITranslator>{});
    AudioTranslationCoordinator audio(capture, asr, translation);
    bool screen = false;
    ProductionInputController controller(settings, capture, asr, audio,
        {[&] { screen = true; }, [&] { screen = false; }, [&] { return screen; }}, true);
    check(!controller.isRunning() && captures == 0 && loads == 0, "restored microphone never auto listens");
    QString feedback;
    QObject::connect(&controller, &ProductionInputController::feedback, [&](const QString &value) { feedback = value; });
    controller.start(); check(wait([&] { return audio.isRunning(); }), "async production start");
    check(loads == 1 && wait([&] { return capture.state() == Audio::State::Running; }), "loaded once capture started");
    controller.stop(); controller.start(); check(audio.isRunning() && loads == 1, "warm stop start");
    settings.setInputMode("system-audio"); check(!controller.isRunning(), "mode change always stops");
    controller.start(); check(audio.isRunning() && capture.selectedDevice().kind == Audio::InputKind::SystemLoopback, "loopback only");
    settings.setInputMode("screen"); controller.start(); check(screen && !audio.isRunning(), "screen excludes audio");
    settings.setInputMode("microphone");
    auto saved = settings.audioSettings(); saved.microphoneId = "gone"; settings.setAudioSettings(saved);
    controller.start(); check(!controller.isRunning() && feedback.contains("unavailable") && settings.audioSettings().microphoneId == "gone", "missing device retained");
    saved.microphoneId = "stable-id"; settings.setAudioSettings(saved);
    TranslationWindow window(settings);
    auto *dialog = window.settingsDialog();
    dialog->setAudioDevices([](auto kind) { return QList<Audio::DeviceInfo>{{"stable-id", "Test device", kind, true}}; });
    dialog->show(); QCoreApplication::processEvents();
    auto *edit = dialog->findChild<QLineEdit *>("speechModelEdit");
    edit->setText("cancelled"); dialog->reject();
    check(settings.audioSettings().modelPath == model, "cancel discards audio draft");
    dialog->show(); QCoreApplication::processEvents();
    dialog->findChild<QComboBox *>("speechLanguageCombo")->setCurrentIndex(2);
    dialog->findChild<QDialogButtonBox *>("settingsButtonBox")->button(QDialogButtonBox::Apply)->click();
    check(settings.audioSettings().language == "zh" && settings.sourceLanguage() == "auto", "apply independent ASR language");
    dialog->reject();
    auto *mode = window.findChild<QComboBox *>("inputModeCombo");
    check(!window.findChild<QPushButton *>("regionButton")->isEnabled(), "region disabled audio");
    GlobalShortcutManager shortcuts;
    int regions = 0;
    OverlayInteractionController overlay(window, settings, shortcuts,
        {[] { return false; }, [&] { ++regions; }, [&] { return controller.isRunning(); }, [&] { controller.start(); },
         [&] { controller.stop(); }, [] {}, [&] { return controller.isScreen(); }});
    OverlayTrayController tray(overlay, window, nullptr, [] { return true; }, false);
    overlay.routeControl(OverlayControlAction::SelectRegion); check(regions == 0, "audio Region no-op");
    mode->setCurrentIndex(0); overlay.routeControl(OverlayControlAction::SelectRegion); check(regions == 1, "screen Region routed");
    QAction *micAction = nullptr;
    for (auto *action : tray.menu()->findChildren<QAction *>()) if (action->objectName() == "trayMode1") micAction = action;
    check(micAction != nullptr, "tray mode action"); if (micAction) micAction->trigger();
    check(settings.inputMode() == "microphone" && mode->currentIndex() == 1, "tray shared selection");
    overlay.routeControl(OverlayControlAction::Start); check(wait([&] { return audio.isRunning(); }), "tray/router audio start");
    overlay.routeControl(OverlayControlAction::HideOverlay); check(audio.isRunning(), "hidden retains pipeline");
    overlay.routeControl(OverlayControlAction::Stop); check(!controller.isRunning() && loads == 1, "router stop warm");
    saved = settings.audioSettings(); saved.modelPath = "missing"; settings.setAudioSettings(saved);
    controller.start(); check(!controller.isRunning() && feedback.contains("not found"), "missing model stops before capture");
    saved.modelPath = model; settings.setAudioSettings(saved);
    controller.start(); check(wait([&] { return audio.isRunning(); }), "model path restored");
    settings.setAudioSettings({"stable-id", "stable-id", model, "ja"});
    check(audio.isRunning() && feedback.contains("Restart"), "running settings require restart");
    controller.stop();
    const auto anotherModel = temp.filePath("other.bin"); QFile::copy(model, anotherModel);
    saved.modelPath = anotherModel; settings.setAudioSettings(saved);
    controller.start(); check(wait([&] { return audio.isRunning(); }) && loads == 2, "changed model reloads asynchronously");
    controller.stop();
    saved.modelPath = model; settings.setAudioSettings(saved);
    controller.start(); controller.stop();
    wait([&] { return !asr.isBusy(); });
    check(!audio.isRunning() && !controller.isRunning(), "Stop during loading cannot auto-start capture");
    ProductionInputController unavailable(settings, capture, asr, audio,
        {[] {}, [] {}, [] { return false; }}, false);
    QObject::connect(&unavailable, &ProductionInputController::feedback, [&](const QString &value) { feedback = value; });
    unavailable.start(); check(!unavailable.isRunning() && feedback.contains("unavailable"), "no Whisper production build feedback");
    return failures ? 1 : 0;
}
