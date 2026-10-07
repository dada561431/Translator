#include "app/ProductionInputController.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

ProductionInputController::ProductionInputController(SettingsManager &settings,
    AudioInputCoordinator &capture, AsrCoordinator &asr, AudioTranslationCoordinator &audio,
    InputPipelineController::OcrActions ocr, bool available, QObject *parent)
    : QObject(parent), settings_(settings), capture_(capture), asr_(asr),
      pipelines_(audio, std::move(ocr)), available_(available)
{
    selectMode(); // Restores selection only; never opens capture or loads a model.
    connect(&settings_, &SettingsManager::inputModeChanged, this, &ProductionInputController::selectMode);
    connect(&settings_, &SettingsManager::audioSettingsChanged, this, [this] {
        if (isRunning()) emit feedback(QStringLiteral("Restart audio to apply changes."));
    });
    connect(&audio, &AudioTranslationCoordinator::runningChanged, this, [this](bool running) {
        emit stateChanged(isRunning());
        emit feedback(running ? QStringLiteral("Listening") : QStringLiteral("Stopped"));
    });
    connect(&audio, &AudioTranslationCoordinator::feedback, this, &ProductionInputController::feedback);
    connect(&asr_, &AsrCoordinator::modelLoaded, this, [this](qint64) {
        if (pendingStart_) beginCapture();
    });
    connect(&asr_, &AsrCoordinator::errorOccurred, this, [this](const Asr::Error &error) {
        if (pendingStart_) { pendingStart_ = false; loadedModel_.clear(); emit stateChanged(false); }
        if (!isScreen()) emit feedback(error.message);
    });
}
ProductionInputController::~ProductionInputController() { stop(); }
QString ProductionInputController::modelPath(const QString &configured, const QString &applicationDir)
{
    for (const auto &candidate : {configured, QDir(applicationDir).filePath(QStringLiteral("models/ggml-base.bin"))}) {
        const QFileInfo file(candidate);
        if (!candidate.isEmpty() && file.isFile() && file.isReadable()) return file.absoluteFilePath();
    }
    return {};
}
void ProductionInputController::selectMode()
{
    stop();
    const auto mode = settings_.inputMode();
    pipelines_.select(mode == QLatin1String("microphone") ? InputPipelineController::Mode::AudioMicrophone
        : mode == QLatin1String("system-audio") ? InputPipelineController::Mode::AudioSystemLoopback
        : InputPipelineController::Mode::ScreenOcr);
    emit stateChanged(false);
}
void ProductionInputController::start()
{
    if (isRunning()) return;
    if (isScreen()) { pipelines_.start(); emit stateChanged(isRunning()); return; }
    if (!available_) { emit feedback(QStringLiteral("Speech recognition is unavailable in this build.")); return; }
    const auto saved = settings_.audioSettings();
    const auto path = modelPath(saved.modelPath, QCoreApplication::applicationDirPath());
    if (path.isEmpty()) {
        emit feedback(saved.modelPath.isEmpty() ? QStringLiteral("Speech model is not installed/configured. Open Settings.")
            : QStringLiteral("ASR model not found. Open Settings.")); return;
    }
    configuration_ = {};
    configuration_.kind = settings_.inputMode() == QLatin1String("microphone") ? Audio::InputKind::Microphone : Audio::InputKind::SystemLoopback;
    configuration_.deviceId = configuration_.kind == Audio::InputKind::Microphone ? saved.microphoneId : saved.outputId;
    configuration_.asr.language = saved.language;
    configuration_.translationSource = settings_.sourceLanguage();
    configuration_.translationTarget = settings_.targetLanguage();
    if (!configuration_.deviceId.isEmpty()) {
        bool found = false;
        for (const auto &device : capture_.devices(configuration_.kind)) found |= device.id == configuration_.deviceId;
        if (!found) { emit feedback(QStringLiteral("Configured device unavailable. Open Settings.")); return; }
    }
    pendingStart_ = true;
    emit stateChanged(true);
    if (loadedModel_ == path && asr_.state() == Asr::State::Error) asr_.cancelUtterance();
    if (loadedModel_ == path && asr_.state() == Asr::State::Ready) { beginCapture(); return; }
    loadedModel_ = path;
    emit feedback(QStringLiteral("Loading speech model..."));
    asr_.loadModel(path);
}
void ProductionInputController::beginCapture()
{
    pendingStart_ = false;
    pipelines_.start(configuration_);
    emit stateChanged(isRunning());
    if (isRunning()) emit feedback(QStringLiteral("Listening - %1").arg(capture_.selectedDevice().description));
}
void ProductionInputController::stop()
{
    pendingStart_ = false;
    // A cancelled load must not be mistaken for a warm model on the next Start.
    if (asr_.state() == Asr::State::Loading) { asr_.unloadModel(); loadedModel_.clear(); }
    pipelines_.stop();
    emit stateChanged(false);
}
