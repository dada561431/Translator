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
        {"endpointing", "energy or legacy-fixed", "mode", "energy"},
        {"pre-roll-ms", "Bounded pre-roll", "ms", "300"},
        {"start-ms", "Consecutive active confirmation", "ms", "100"},
        {"minimum-speech-ms", "Minimum active audio", "ms", "250"},
        {"trailing-silence-ms", "End confirmation", "ms", "600"},
        {"max-utterance-ms", "Safety cap", "ms", "12000"},
        {"minimum-rms", "Minimum start RMS", "rms", "0.002"},
        {"noise-ratio", "Start/noise ratio", "ratio", "3"},
        {"verbose-endpointing", "Per-chunk RMS diagnostics (QA only)"},
        {"play-once", "Play local speech once, then remain silent"},
        {"pause-after-ms", "QA pause playback during each loopback cycle", "ms"},
        {"phrase-cues", "QA-only six timed microphone prompts; owner must confirm spoken ground truth"},
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
    const auto endpointing = parser.value("endpointing");
    if (endpointing != "energy" && endpointing != "legacy-fixed") return 2;
    if (kind == "microphone" && !parser.isSet("device")) {
        print({{"event", "configuration_error"}, {"error", "Microphone QA requires an explicit device ID; no default virtual input."}}); return 2;
    }
    config.segmentation = endpointing == "energy" ? AudioTranslationCoordinator::Segmentation::Energy
                                                 : AudioTranslationCoordinator::Segmentation::LegacyFixed;
    bool validConfig = true;
    auto number = [&](const char *name) { bool ok; const int n = parser.value(name).toInt(&ok); validConfig &= ok; return n; };
    config.endpoint.preRollMs = number("pre-roll-ms"); config.endpoint.startMs = number("start-ms");
    config.endpoint.minimumSpeechMs = number("minimum-speech-ms");
    config.endpoint.trailingSilenceMs = number("trailing-silence-ms"); config.endpoint.maxUtteranceMs = number("max-utterance-ms");
    bool okRms, okRatio;
    config.endpoint.minimumRms = parser.value("minimum-rms").toDouble(&okRms);
    config.endpoint.noiseRatio = parser.value("noise-ratio").toDouble(&okRatio);
    if (!validConfig || !okRms || !okRatio || !config.endpoint.valid()) return 2;
    QObject::connect(&pipeline, &AudioTranslationCoordinator::endpointEvent, [&](const QString &event) {
        print({{"event", "endpoint"}, {"transition", event}, {"monotonic_us", Audio::monotonicUs()}});
    });
    if (parser.isSet("verbose-endpointing"))
        QObject::connect(&pipeline, &AudioTranslationCoordinator::endpointDetails, [&](double rms, double floor, double threshold, int state) {
            print({{"event", "energy"}, {"rms", rms}, {"noise_floor", floor}, {"start_threshold", threshold}, {"state", state}});
        });
    QObject::connect(&pipeline, &AudioTranslationCoordinator::latencyMeasured,
        [&](auto session, auto utterance, const QString &stage, qint64 speechEnd, qint64 boundary, qint64 now, qint64 processing) {
            const bool valid = speechEnd > 0 && now >= speechEnd && boundary >= speechEnd;
            QJsonObject o{{"event", "latency"}, {"stage", stage}, {"session", qint64(session)}, {"utterance", qint64(utterance)},
                {"estimated_speech_end_us", speechEnd}, {"boundary_us", boundary}, {"result_us", now}, {"processing_ms", processing},
                {"estimate_valid", valid}};
            o.insert("speech_end_detection_delay_ms", valid ? QJsonValue((boundary - speechEnd) / 1000) : QJsonValue());
            const QString key = stage == "translation" ? "speech_end_to_translation_ms" : "speech_end_to_asr_final_ms";
            if (stage != "boundary") o.insert(key, valid ? QJsonValue((now - speechEnd) / 1000) : QJsonValue());
            print(o);
        });
    config.translationSource = settings.sourceLanguage(); config.translationTarget = settings.targetLanguage();
    QAudioOutput output; QMediaPlayer player;
    if (parser.isSet("play")) {
        player.setAudioOutput(&output); player.setLoops(parser.isSet("play-once") ? 1 : QMediaPlayer::Infinite);
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
    QLabel cue;
    cue.setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
    cue.setMinimumSize(700, 140); cue.move(80, 80); cue.setWordWrap(true);
    auto cueFont = cue.font(); cueFont.setPointSize(20); cue.setFont(cueFont);
    const QStringList phrases{"Hello", "Translator", "Testing", "Good morning", "How are you today?", "This is a microphone recognition test."};
    if (parser.isSet("phrase-cues") && (kind != "microphone" || seconds < 55 || cycles != 1)) return 2;
    std::function<void()> startCycle = [&] {
        ++cycle; if (!input.start(config)) { app.exit(4); return; }
        if (parser.isSet("play")) { player.setPosition(0); player.play(); }
        if (parser.isSet("pause-after-ms")) {
            bool validPause; const auto pauseMs = parser.value("pause-after-ms").toInt(&validPause);
            if (!validPause || pauseMs < 1 || pauseMs >= seconds * 1000 || !parser.isSet("play")) { app.exit(2); return; }
            QTimer::singleShot(pauseMs, &player, [&] {
                player.pause(); print({{"event", "playback_paused"}, {"monotonic_us", Audio::monotonicUs()}});
            });
        }
        print({{"event", "cycle_started"}, {"cycle", cycle}, {"model_loads", loads}, {"kind", kind}, {"endpointing", endpointing}});
        if (parser.isSet("phrase-cues")) {
            cue.setText("Microphone QA: read each prompt once, then remain quiet."); cue.show();
            for (int i = 0; i < phrases.size(); ++i) QTimer::singleShot(2000 + i * 8000, &cue, [&, i] {
                cue.setText(QStringLiteral("%1: %2\nRead once naturally; then remain quiet.").arg(endpointing, phrases.at(i)));
                print({{"event", "phrase_cue"}, {"expected_prompt", phrases.at(i)}, {"index", i + 1}, {"monotonic_us", Audio::monotonicUs()}});
            });
            QTimer::singleShot(50000, &cue, [&] { cue.setText("QA ending: remain quiet. Confirm actual spoken phrases with the agent."); });
        }
        cycleTimer.start(seconds * 1000);
    };
    QObject::connect(&cycleTimer, &QTimer::timeout, [&] {
        player.pause();
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
