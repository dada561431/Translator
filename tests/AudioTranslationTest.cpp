#include "app/InputPipelineController.h"
#include <QApplication>
#include <QLabel>
#include "gui/TranslationWindow.h"
#include <QTemporaryDir>
#include <QSettings>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <iostream>
#include "config/SettingsManager.h"

namespace {
int failures = 0;
void check(bool ok, const char *name) { if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; } }
bool wait(const std::function<bool()> &predicate, int ms = 2000) {
    QElapsedTimer t; t.start();
    while (!predicate() && t.elapsed() < ms) { QCoreApplication::processEvents(); QThread::msleep(1); }
    QCoreApplication::processEvents(); return predicate();
}
struct Stats { std::atomic_int loads{0}, calls{0}, destroyed{0}; std::atomic_bool hold{false}, fail{false}, empty{false}; };
class Backend final : public IAsrBackend {
public:
    std::shared_ptr<Stats> stats;
    explicit Backend(std::shared_ptr<Stats> s) : stats(std::move(s)) {}
    ~Backend() override { ++stats->destroyed; }
    Asr::Error loadModel(const QString &, const Asr::CancelToken &) override { ++stats->loads; return {}; }
    void unloadModel() override {}
    Asr::BackendResult recognize(const std::vector<float> &, const Asr::Options &, const Asr::CancelToken &token) override {
        const int call = ++stats->calls;
        while (stats->hold && !token->requested()) QThread::msleep(1);
        if (stats->fail) return {{}, {}, {Asr::ErrorCode::BackendFailure, "inference failed"}};
        return {stats->empty ? QString() : QStringLiteral("sentence %1").arg(call), "en", {}};
    }
};
class Input final : public IAudioInput {
public:
    bool active = false;
    Audio::InputKind kind;
    explicit Input(Audio::InputKind k) : kind(k) {}
    QList<Audio::DeviceInfo> devices() const override { return {{"default", "fake", kind, true}}; }
    void start(const QByteArray &) override { active = true; emit started(); }
    void stop() override { if (active) { active = false; emit stopped(); } }
    bool running() const override { return active; }
    Audio::NativeFormat nativeFormat() const override { return {16000, 1, Audio::SampleType::Int16}; }
    Audio::DeviceInfo selectedDevice() const override { return devices().front(); }
};
class Translator final : public ITranslator {
public:
    QList<TranslationRequest> requests;
    QString id() const override { return "test"; }
    void translate(const TranslationRequest &r) override { requests.append(r); }
    void complete(int i, bool success = true) {
        const auto r = requests.at(i); TranslationResult v;
        v.requestId = r.requestId; v.sourceText = r.sourceText; v.success = success;
        v.translatedText = "TR:" + r.sourceText; v.error = success ? QString() : QStringLiteral("test timeout");
        emit resultReady(v);
    }
};
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv); QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    app.setOrganizationName("Phase8CTest"); app.setApplicationName("Isolated");
    SettingsManager settings; settings.setTranslator("deepl");
    Input *input = nullptr; auto stats = std::make_shared<Stats>();
    {
        AudioInputCoordinator audio(nullptr, [&](auto kind) { auto p = std::make_unique<Input>(kind); input = p.get(); return p; });
        AsrCoordinator asr([stats] { return std::make_unique<Backend>(stats); });
        auto provider = std::make_unique<Translator>(); auto *translator = provider.get();
        TranslationCoordinator translation(settings, std::move(provider));
        AudioTranslationCoordinator pipeline(audio, asr, translation);
        TranslationWindow window(settings); window.show();
        QObject::connect(&pipeline, &AudioTranslationCoordinator::originalTextReady, &window, &TranslationWindow::setOriginalText);
        QObject::connect(&pipeline, &AudioTranslationCoordinator::translatedTextReady, &window, &TranslationWindow::setTranslatedText);
        AudioTranslationCoordinator::Configuration config; config.segmentMs = 1000;
        config.translationSource = "en"; config.translationTarget = "zh";
        QString original, translated; int originals = 0, finals = 0, errors = 0, boundaries = 0;
        Asr::Result last;
        QObject::connect(&pipeline, &AudioTranslationCoordinator::originalTextReady, [&](auto text) { original = text; ++originals; });
        QObject::connect(&pipeline, &AudioTranslationCoordinator::translatedTextReady, [&](auto text) { translated = text; });
        QObject::connect(&pipeline, &AudioTranslationCoordinator::feedback, [&](auto) { ++errors; });
        QObject::connect(&pipeline, &AudioTranslationCoordinator::finalReady, [&](auto, auto, auto r) { last = r; ++finals; });
        QObject::connect(&pipeline, &AudioTranslationCoordinator::segmentFinalized, [&](auto, auto, auto) { ++boundaries; });
        check(!pipeline.isRunning() && !input && stats->loads == 0, "initial stopped/privacy");
        check(!pipeline.start(config), "model required before capture");
        asr.loadModel("fake"); check(wait([&] { return asr.state() == Asr::State::Ready; }), "model ready");
        bool ocr = true; int ocrStops = 0;
        InputPipelineController modes(pipeline, {[&] { ocr = true; }, [&] { ocr = false; ++ocrStops; }, [&] { return ocr; }});
        modes.select(InputPipelineController::Mode::AudioMicrophone);
        check(!ocr && modes.start(config) && ocrStops > 0, "mode switch stops OCR; microphone start");
        check(!modes.start(config), "duplicate start rejected");
        check(wait([&] { return audio.state() == Audio::State::Running; }), "audio running");
        emit audio.pcmReady({QByteArray(640, '\0'), audio.session() + 1, 0, 0, false});
        check(pipeline.bufferedChunks() == 0, "old audio capture session rejected");
        quint64 seq = 0; int delivered = 0;
        QObject::connect(&audio, &AudioInputCoordinator::pcmReady, [&](auto) { ++delivered; });
        auto feed = [&] {
            const int before = delivered;
            emit input->pcmReady({QByteArray(640, '\0'), 999, seq, qint64(seq) * 20000, seq == 1}); ++seq;
            check(wait([&] { return delivered > before; }, 200), "PCM delivery");
        };
        auto segment = [&] { const int old = finals; feed(); pipeline.finalizeBoundary(); check(wait([&] { return finals > old; }), "next Final"); };
        stats->hold = true; feed(); pipeline.finalizeBoundary();
        check(wait([&] { return asr.isBusy(); }), "active utterance for Partial guard");
        Asr::Result preview; preview.kind = Asr::ResultKind::Partial; preview.session = asr.session(); preview.utterance = asr.utterance();
        preview.text = "hel"; emit asr.resultReady(preview); preview.text = "hello"; emit asr.resultReady(preview);
        check(translator->requests.isEmpty() && originals == 0, "active Partial-only yields zero requests and Original updates");
        stats->hold = false;
        check(wait([&] { return translator->requests.size() == 1; }), "PCM forwarded, Final translated exactly once");
        check(originals == 1 && original == translator->requests.front().sourceText, "Original immediate");
        check(translator->requests.front().sourceLanguage == "en" && settings.sourceLanguage() == "auto",
              "explicit translation language does not rewrite settings/detected language");
        check(window.findChild<QLabel *>("originalLabel")->text() == original, "existing subtitle window Original binding");
        auto partial = last; partial.kind = Asr::ResultKind::Partial;
        emit asr.resultReady(partial); partial.text = "hello"; emit asr.resultReady(partial);
        emit asr.resultReady(last);
        check(translator->requests.size() == 1 && originals == 1, "Partial and duplicate Final never translated/displayed");
        segment(); check(last.discontinuity && last.audioDurationMs == 20, "PCM metadata retained, duration uses actual chunks");
        translator->complete(0); check(translated.isEmpty(), "old pending response cannot update newer Original");
        translator->complete(1); const auto newest = translated; translator->complete(0);
        check(translated == newest && newest == "TR:" + original, "out-of-order translation never moves subtitles backwards");
        check(window.findChild<QLabel *>("translatedLabel")->text() == newest, "existing subtitle window translation binding");
        segment(); translator->complete(2, false);
        check(pipeline.isRunning() && errors > 0, "translation failure nonfatal");
        segment(); translator->complete(3); check(translated == "TR:" + original, "translation recovery");
        stats->empty = true; const int requests = translator->requests.size(), oldOriginals = originals;
        segment(); check(originals == oldOriginals && translator->requests.size() == requests, "empty Final preserves subtitles"); stats->empty = false;
        segment(); const int stoppedRequest = translator->requests.size() - 1;
        pipeline.stop(); pipeline.stop(); const auto prior = translated;
        translator->complete(stoppedRequest); emit asr.resultReady(last);
        check(!pipeline.isRunning() && audio.state() == Audio::State::Stopped && translated == prior, "Stop stale and subtitle preservation");
        for (int i = 0; i < 5; ++i) { check(pipeline.start(config), "warm restart"); pipeline.stop(); }
        check(stats->loads == 1, "five Stop/Start cycles keep model warm");
        modes.select(InputPipelineController::Mode::AudioSystemLoopback); check(modes.start(config), "loopback routing");
        check(input->kind == Audio::InputKind::SystemLoopback, "independent loopback backend selected");
        wait([&] { return audio.state() == Audio::State::Running; }); seq = 0;
        segment(); translator->complete(translator->requests.size() - 1); const auto restarted = translated;
        translator->complete(stoppedRequest); check(translated == restarted, "previous session translation suppressed after restart");
        // Timer requests boundaries using actual received PCM, not an assumed 200 chunks.
        const int oldBoundaries = boundaries; feed();
        check(wait([&] { return boundaries > oldBoundaries; }, 1500), "segmentation timer finalizes");
        wait([&] { return !asr.isBusy(); });
        stats->hold = true; feed(); pipeline.finalizeBoundary();
        check(wait([&] { return asr.isBusy(); }), "in-flight ASR");
        for (int i = 0; i < 6; ++i) { feed(); pipeline.finalizeBoundary(); }
        check(pipeline.droppedSegments() >= 5 && pipeline.bufferedChunks() <= 1500, "bounded latest pending backpressure");
        pipeline.stop(); stats->hold = false;
        wait([&] { return !asr.isBusy(); });
        config.asr.timeoutMs = 30; modes.start(config); wait([&] { return audio.state() == Audio::State::Running; }); seq = 0;
        stats->hold = true; const int beforeErrors = errors; feed(); pipeline.finalizeBoundary();
        check(wait([&] { return errors > beforeErrors; }), "ASR timeout reported");
        stats->hold = false; wait([&] { return !asr.isBusy(); });
        config.asr.timeoutMs = 60000; segment(); check(pipeline.isRunning(), "ASR timeout next utterance recovers");
        stats->fail = true; const int beforeFailure = errors; feed(); pipeline.finalizeBoundary();
        check(wait([&] { return errors > beforeFailure; }), "ASR backend failure");
        stats->fail = false; wait([&] { return !asr.isBusy(); }); segment();
        settings.setTranslator("none"); const int noneRequests = translator->requests.size(); segment();
        check(pipeline.isRunning() && translator->requests.size() == noneRequests, "provider None preserves Original and capture");
        modes.select(InputPipelineController::Mode::ScreenOcr); check(modes.start() && !pipeline.isRunning(), "switch to OCR cancels audio");
        modes.select(InputPipelineController::Mode::AudioMicrophone); modes.start(config); wait([&] { return audio.state() == Audio::State::Running; });
        emit input->errorOccurred({Audio::ErrorCode::DeviceUnavailable, "unplugged", {}});
        check(wait([&] { return !pipeline.isRunning(); }), "device failure stops pipeline");
        check(asr.state() == Asr::State::Ready && stats->loads == 1, "failure keeps model reusable");
        pipeline.start(config); wait([&] { return audio.state() == Audio::State::Running; });
        seq = 0; stats->hold = true; feed(); pipeline.finalizeBoundary();
        check(wait([&] { return asr.isBusy(); }), "shutdown during active inference");
    }
    check(stats->destroyed == 1, "shutdown releases worker backend");
    std::cout << "Phase8C failures=" << failures << '\n'; return failures ? 1 : 0;
}
