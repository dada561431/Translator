#include "app/InputPipelineController.h"
#include "asr/WhisperCppAsrBackend.h"
#include "config/SettingsManager.h"
#include "gui/TranslationWindow.h"
#include "translator/TranslatorFactory.h"
#include "credentials/ICredentialStore.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTemporaryDir>
#include <QSettings>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QLabel>
#include <iostream>

namespace {
void print(const QJsonObject &o) { std::cout << QJsonDocument(o).toJson(QJsonDocument::Compact).constData() << std::endl; }
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser; parser.addHelpOption();
    parser.setApplicationDescription("Phase 8C explicit audio QA; no recording and no credential arguments.");
    parser.addOptions({{"kind", "microphone or loopback", "kind"}, {"model", "Existing model path", "path"},
        {"language", "ASR language", "language", "en"}, {"source", "Translation source", "source", "en"},
        {"target", "Translation target", "target", "zh"}, {"provider", "none, deepl or openai_compatible", "provider", "none"},
        {"seconds", "Seconds per capture cycle (1-600)", "seconds", "24"},
        {"cycles", "Warm Stop/Start cycles (1-10)", "n", "1"},
        {"segment-seconds", "Non-overlapping segment window (1-30)", "n", "4"},
        {"play", "Explicit local short speech playback, loopback only", "wav"},
        {"device", "Microphone stable hex ID or loopback UTF-8 endpoint", "id"},
        {"exit-on-translation", "QA exit with a real network request pending"},
        {"exit-on-inference", "QA close just after a segment is submitted"},
        {"exit-while-running", "Exercise destructor cleanup during capture/inference"}});
    parser.process(app);
    const auto kind = parser.value("kind"), provider = parser.value("provider");
    bool okSeconds, okCycles, okWindow;
    const int seconds = parser.value("seconds").toInt(&okSeconds), cycles = parser.value("cycles").toInt(&okCycles),
        window = parser.value("segment-seconds").toInt(&okWindow);
    if ((kind != "microphone" && kind != "loopback") || !parser.isSet("model")
        || !okSeconds || seconds < 1 || seconds > 600 || !okCycles || cycles < 1 || cycles > 10
        || !okWindow || window < 1 || window > 30 || (parser.isSet("play") && kind != "loopback")
        || (provider != "none" && provider != "deepl" && provider != "openai_compatible")) return 2;
    // Copy only non-secret provider configuration; QA writes never touch user preferences.
    QSettings existing(QSettings::NativeFormat, QSettings::UserScope, "TranslatorProject", "Translator");
    const auto plan = existing.value("translator/deepl/plan", "free").toString();
    const auto base = existing.value("translator/openaiCompatible/baseUrl").toString();
    const auto model = existing.value("translator/openaiCompatible/model").toString();
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    app.setOrganizationName("TranslatorAudioQA"); app.setApplicationName("Phase8C");
    SettingsManager settings; settings.setTranslator(provider); settings.setDeepLPlan(plan);
    if (!base.isEmpty()) settings.setOpenAiBaseUrl(base);
    if (!model.isEmpty()) settings.setOpenAiModel(model);
    settings.setSourceLanguage(parser.value("source")); settings.setTargetLanguage(parser.value("target"));
    auto credentials = createPlatformCredentialStore();
    TranslationWindow overlay(settings, nullptr, credentials.get()); overlay.show();
    AudioInputCoordinator audio;
    AsrCoordinator asr([] { return std::make_unique<WhisperCppAsrBackend>(); });
    TranslationCoordinator translation(settings, [&] { return TranslatorFactory::create(settings, *credentials); });
    AudioTranslationCoordinator pipeline(audio, asr, translation);
    // This dedicated QA executable has no OCR source; normal Translator stays OCR-only.
    InputPipelineController input(pipeline, {[] {}, [] {}, [] { return false; }});
    input.select(kind == "microphone" ? InputPipelineController::Mode::AudioMicrophone
                                       : InputPipelineController::Mode::AudioSystemLoopback);
    AudioTranslationCoordinator::Configuration config;
    config.kind = kind == "microphone" ? Audio::InputKind::Microphone : Audio::InputKind::SystemLoopback;
    config.deviceId = kind == "microphone" ? QByteArray::fromHex(parser.value("device").toLatin1()) : parser.value("device").toUtf8();
    if (parser.isSet("device") && (config.deviceId.isEmpty() || (kind == "microphone"
        && config.deviceId.toHex() != parser.value("device").toLatin1().toLower()))) return 2;
    config.asr.language = parser.value("language"); config.segmentMs = window * 1000;
    config.translationSource = settings.sourceLanguage(); config.translationTarget = settings.targetLanguage();
    QAudioOutput output; QMediaPlayer player;
    if (parser.isSet("play")) {
        player.setAudioOutput(&output); player.setLoops(QMediaPlayer::Infinite);
        player.setSource(QUrl::fromLocalFile(parser.value("play")));
        QObject::connect(&player, &QMediaPlayer::errorOccurred, [&](auto, const QString &error) { print({{"event", "playback_error"}, {"error", error}}); });
    }
    QObject::connect(&overlay, &TranslationWindow::closed, &app, &QCoreApplication::quit);
    QObject::connect(&overlay, &TranslationWindow::stopRequested, &pipeline, &AudioTranslationCoordinator::stop);
    QObject::connect(&overlay, &TranslationWindow::startRequested, [&] { input.start(config); });
    QObject::connect(&pipeline, &AudioTranslationCoordinator::runningChanged, &overlay, &TranslationWindow::setTranslationRunning);
    QObject::connect(&pipeline, &AudioTranslationCoordinator::originalTextReady, &overlay, &TranslationWindow::setOriginalText);
    QObject::connect(&pipeline, &AudioTranslationCoordinator::translatedTextReady, &overlay, &TranslationWindow::setTranslatedText);
    auto verifySubtitle = [&](const QString &text, const QString &field) {
        bool found = false;
        for (auto *label : overlay.findChildren<QLabel *>()) if (label->text() == text && label->isVisible()) found = true;
        print({{"event", "subtitle_display"}, {"field", field}, {"visible_label_matches", found}, {"text", text}});
    };
    QObject::connect(&pipeline, &AudioTranslationCoordinator::originalTextReady, [&](auto text) { verifySubtitle(text, "original"); });
    QObject::connect(&pipeline, &AudioTranslationCoordinator::translatedTextReady, [&](auto text) { verifySubtitle(text, "translation"); });
    QObject::connect(&pipeline, &AudioTranslationCoordinator::feedback, &overlay, &TranslationWindow::setRegionFeedback);
    int loads = 0, finals = 0, requests = 0, translated = 0, cycle = 0;
    QObject::connect(&audio, &AudioInputCoordinator::stateChanged, [&](auto state) {
        print({{"event", "audio_state"}, {"state", int(state)}, {"session", qint64(audio.session())},
               {"device", audio.selectedDevice().description}, {"native", Audio::describe(audio.nativeFormat())}});
    });
    QObject::connect(&pipeline, &AudioTranslationCoordinator::feedback, [&](auto message) { print({{"event", "feedback"}, {"message", message}}); });
    QObject::connect(&pipeline, &AudioTranslationCoordinator::segmentFinalized, [&](auto session, auto utterance, auto ms) {
        print({{"event", "boundary"}, {"session", qint64(session)}, {"utterance", qint64(utterance)}, {"duration_ms", ms}});
        if (parser.isSet("exit-on-inference")) QTimer::singleShot(100, &overlay, &QWidget::close);
    });
    QObject::connect(&pipeline, &AudioTranslationCoordinator::finalReady, [&](auto session, auto utterance, const auto &r) {
        ++finals; print({{"event", "final"}, {"session", qint64(session)}, {"utterance", qint64(utterance)},
            {"asr_session", qint64(r.session)}, {"asr_utterance", qint64(r.utterance)}, {"text", r.text},
            {"asr_ms", r.processingMs}, {"audio_ms", r.audioDurationMs}, {"discontinuity", r.discontinuity}});
    });
    QObject::connect(&pipeline, &AudioTranslationCoordinator::translationRequested, [&](auto session, auto utterance, const auto &r) {
        ++requests; print({{"event", "translation_request"}, {"session", qint64(session)}, {"utterance", qint64(utterance)},
            {"request", qint64(r.requestId)}, {"text", r.sourceText}});
        if (parser.isSet("exit-on-translation")) QTimer::singleShot(50, &overlay, &QWidget::close);
    });
    QObject::connect(&pipeline, &AudioTranslationCoordinator::translationFinished, [&](auto session, auto utterance, const auto &r, auto delay) {
        if (r.success) ++translated;
        print({{"event", "translation_result"}, {"session", qint64(session)}, {"utterance", qint64(utterance)},
            {"request", qint64(r.requestId)}, {"success", r.success}, {"text", r.translatedText}, {"error", r.error},
            {"translation_ms", r.elapsedMs}, {"post_boundary_ms", delay}});
    });
    QObject::connect(&asr, &AsrCoordinator::errorOccurred, [&](const auto &error) {
        print({{"event", "asr_error"}, {"error", error.message}});
        if (!loads) app.exit(3);
    });
    QTimer cycleTimer; cycleTimer.setSingleShot(true);
    std::function<void()> startCycle = [&] {
        ++cycle; if (!input.start(config)) { app.exit(4); return; }
        if (parser.isSet("play")) player.play();
        print({{"event", "cycle_started"}, {"cycle", cycle}, {"model_loads", loads}, {"kind", kind}});
        cycleTimer.start(seconds * 1000);
    };
    QObject::connect(&cycleTimer, &QTimer::timeout, [&] {
        if (!parser.isSet("exit-while-running")) pipeline.stop();
        if (cycle < cycles) QTimer::singleShot(500, &app, startCycle);
        else app.quit();
    });
    QObject::connect(&asr, &AsrCoordinator::modelLoaded, [&](auto ms) {
        ++loads; print({{"event", "model_loaded"}, {"load_ms", ms}, {"loads", loads}}); startCycle();
    });
    QTimer::singleShot(0, &app, [&] { asr.loadModel(parser.value("model")); });
    QTimer::singleShot(120000 + cycles * (seconds + 1) * 1000, &app, [&] { app.exit(5); });
    const int code = app.exec();
    player.stop(); pipeline.stop();
    print({{"event", "summary"}, {"cycles", cycle}, {"model_loads", loads}, {"finals", finals},
        {"requests", requests}, {"translated", translated}, {"dropped_segments", qint64(pipeline.droppedSegments())},
        {"audio_stopped", audio.state() == Audio::State::Stopped}, {"exit", code}});
    return code;
}
