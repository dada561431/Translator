#include "asr/AsrCoordinator.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QEventLoop>
#include <atomic>
#include <mutex>
#include <iostream>

namespace {
struct Stats {
    std::atomic_int loads{0}, unloads{0}, calls{0}, destroyed{0};
    std::atomic_bool hold{false}, ignoreCancel{false}, badThread{false};
    std::atomic_bool throwInference{false};
    Qt::HANDLE owner = QThread::currentThreadId();
    std::mutex mutex;
    QList<int> sizes;
};
class Fake final : public IAsrBackend {
public:
    explicit Fake(std::shared_ptr<Stats> stats) : stats_(std::move(stats)) {}
    ~Fake() override { threadCheck(); ++stats_->destroyed; }
    Asr::Error loadModel(const QString &path, const Asr::CancelToken &) override {
        threadCheck(); ++stats_->loads;
        if (path.endsWith(QLatin1String("bad"))) return {Asr::ErrorCode::ModelLoadFailed, QStringLiteral("bad model")};
        return {};
    }
    void unloadModel() override { threadCheck(); ++stats_->unloads; }
    Asr::BackendResult recognize(const std::vector<float> &pcm, const Asr::Options &, const Asr::CancelToken &token) override {
        threadCheck(); ++stats_->calls;
        if (stats_->throwInference) throw std::runtime_error("fake backend failure");
        { std::lock_guard<std::mutex> lock(stats_->mutex); stats_->sizes.append(int(pcm.size())); }
        while (stats_->hold && (stats_->ignoreCancel || !token->requested())) QThread::msleep(1);
        return {QStringLiteral("  source speech  "), QStringLiteral("en"), {}};
    }
private:
    void threadCheck() { if (stats_->owner == QThread::currentThreadId()) stats_->badThread = true; }
    std::shared_ptr<Stats> stats_;
};
bool wait(const std::function<bool()> &predicate, int timeout = 3000) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
    }
    QCoreApplication::processEvents(); return predicate();
}
Audio::PcmChunk chunk(quint64 seq, quint64 session = 10, bool discontinuity = false) {
    return {QByteArray(640, '\0'), session, seq, qint64(seq) * 20000, discontinuity};
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *name) { if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; } };
    auto stats = std::make_shared<Stats>();
    {
        AsrCoordinator c([stats] { return std::make_unique<Fake>(stats); });
        QList<Asr::Result> results; QList<Asr::Error> errors;
        QObject::connect(&c, &AsrCoordinator::resultReady, [&](const auto &r) { results.append(r); });
        QObject::connect(&c, &AsrCoordinator::errorOccurred, [&](const auto &e) { errors.append(e); });
        check(c.state() == Asr::State::Unloaded && stats->loads == 0, "initial unloaded, no startup model");
        check(!c.beginUtterance() && errors.back().code == Asr::ErrorCode::ModelNotLoaded, "missing model");
        c.loadModel("bad"); check(wait([&] { return c.state() == Asr::State::Error; }), "load failure");
        c.loadModel("A"); c.loadModel("A"); check(wait([&] { return c.state() == Asr::State::Ready; }), "load recovery");
        const int loads = stats->loads;
        c.loadModel("A"); check(stats->loads == loads && !c.isBusy(), "same model idempotent");
        check(!c.beginUtterance({QStringLiteral("xx")}) && errors.back().code == Asr::ErrorCode::UnsupportedLanguage, "no language fallback");
        for (const auto &lang : {"auto", "en", "zh", "ja", "ko"}) {
            check(c.beginUtterance({QLatin1String(lang)}), "language accepted");
            c.cancelUtterance();
        }
        check(c.beginUtterance() && !c.finalizeUtterance() && errors.back().code == Asr::ErrorCode::EmptyAudio, "empty explicit error");
        check(c.pushPcm(chunk(0)) && c.requestPartial(), "partial start");
        check(wait([&] { return results.size() == 1; }), "partial delivery");
        check(results.back().kind == Asr::ResultKind::Partial && results.back().text == QLatin1String("source speech")
            && results.back().revision == 1 && results.back().session == c.session(), "result metadata and trim");
        check(c.pushPcm(chunk(1)) && c.requestPartial(), "second partial");
        check(wait([&] { return results.size() == 2; }) && results.back().revision == 2, "revision ordering");
        c.finalizeUtterance(); check(wait([&] { return results.size() == 3; }), "final emitted");
        check(results.back().kind == Asr::ResultKind::Final && !c.pushPcm(chunk(2)) && !c.bufferedBytes(), "final closes and clears buffer");

        stats->hold = stats->ignoreCancel = true;
        const int running = stats->calls + 1;
        c.beginUtterance(); c.pushPcm(chunk(0)); c.requestPartial();
        check(wait([&] { return stats->calls == running; }), "controlled in-flight partial");
        for (int i = 1; i < 4; ++i) { c.pushPcm(chunk(quint64(i))); c.requestPartial(); }
        check(c.pendingJobs() == 1 && stats->calls == running, "partial coalescing bounded");
        stats->hold = false;
        check(wait([&] { return results.size() == 5 && !c.isBusy(); }), "current plus latest only");
        check(stats->calls == running + 1 && results.back().revision == 4, "middle revisions not inferred");
        { std::lock_guard<std::mutex> lock(stats->mutex); check(stats->sizes.back() == 1280, "latest pending PCM snapshot retained"); }
        stats->hold = true;
        const int finalRunning = stats->calls + 1;
        c.beginUtterance(); c.pushPcm(chunk(0)); c.requestPartial();
        check(wait([&] { return stats->calls == finalRunning; }), "partial running for final priority");
        c.pushPcm(chunk(1)); c.requestPartial(); c.pushPcm(chunk(2)); c.finalizeUtterance();
        const int before = int(results.size());
        stats->hold = false;
        check(wait([&] { return !c.isBusy(); }) && results.size() == before + 1
            && results.back().kind == Asr::ResultKind::Final && stats->calls == finalRunning + 1, "final replaces pending and suppresses aborted partial");

        stats->hold = true;
        const int cancelRunning = stats->calls + 1;
        c.beginUtterance(); c.pushPcm(chunk(0)); c.finalizeUtterance();
        check(wait([&] { return stats->calls == cancelRunning; }), "final running for cancel");
        const auto oldSession = c.session(); const int oldResults = int(results.size());
        c.cancelUtterance(); check(c.session() > oldSession && !c.bufferedBytes(), "cancel invalidates and clears");
        c.beginUtterance(); c.pushPcm(chunk(0)); c.finalizeUtterance(); stats->hold = false;
        check(wait([&] { return !c.isBusy(); }) && results.size() == oldResults + 1
            && results.back().session == c.session(), "stale final suppressed and new utterance recovers");

        stats->hold = true;
        const int timeoutRunning = stats->calls + 1;
        c.beginUtterance({QStringLiteral("en"), 20}); c.pushPcm(chunk(0)); c.requestPartial();
        check(wait([&] { return stats->calls == timeoutRunning; }), "timeout job started");
        const int timeoutResults = int(results.size());
        check(wait([&] { return c.state() == Asr::State::Error; }) && errors.back().code == Asr::ErrorCode::Timeout, "deadline event");
        stats->hold = false;
        check(wait([&] { return !c.isBusy(); }) && results.size() == timeoutResults, "late timeout result suppressed");
        check(c.beginUtterance(), "timeout recovery begin"); c.pushPcm(chunk(0, 10, true)); c.pushPcm(chunk(2));
        check(!c.pushPcm(chunk(2)) && !c.pushPcm(chunk(3, 20)), "duplicates and cross-session rejected");
        c.finalizeUtterance(); check(wait([&] { return results.size() == timeoutResults + 1; }) && results.back().discontinuity, "discontinuity in result");
        stats->throwInference = true;
        c.beginUtterance(); c.pushPcm(chunk(0)); c.finalizeUtterance();
        check(wait([&] { return !c.isBusy(); }) && c.state() == Asr::State::Error
            && errors.back().code == Asr::ErrorCode::BackendFailure, "worker exceptions become errors");
        stats->throwInference = false;
        c.beginUtterance({QStringLiteral("en"), 20}); c.pushPcm(chunk(0)); c.requestPartial();
        const int delayedResults = int(results.size());
        QThread::msleep(60); // Deadline must apply even if the owner event loop was delayed.
        check(wait([&] { return !c.isBusy(); }) && results.size() == delayedResults
            && errors.back().code == Asr::ErrorCode::Timeout, "late delivery rejected by steady deadline");
        c.beginUtterance();
        for (int i = 0; i < 1500; ++i) check(c.pushPcm(chunk(quint64(i))), "bounded accumulation");
        check(!c.pushPcm(chunk(1500)) && c.bufferedBytes() == 960000 && errors.back().code == Asr::ErrorCode::UtteranceTooLong, "maximum utterance error");
        c.cancelUtterance();
        c.loadModel("B"); check(wait([&] { return c.state() == Asr::State::Ready; }) && stats->loads == loads + 1, "model reload unload then load");
        c.beginUtterance(); c.pushPcm(chunk(0)); c.finalizeUtterance(); check(wait([&] { return !c.isBusy(); }), "B inference");
        stats->hold = true;
        const int reloadRunning = stats->calls + 1, reloadResults = int(results.size());
        c.beginUtterance(); c.pushPcm(chunk(0)); c.finalizeUtterance();
        check(wait([&] { return stats->calls == reloadRunning; }), "reload during inference setup");
        c.loadModel("C"); c.loadModel("C");
        check(c.state() == Asr::State::Loading && stats->loads == loads + 1, "reload waits for old worker, no parallel context");
        stats->hold = false;
        check(wait([&] { return c.state() == Asr::State::Ready; })
            && results.size() == reloadResults && stats->loads == loads + 2, "reload suppresses old result and coalesces load");
        c.stop(); c.stop(); check(wait([&] { return !c.isBusy(); }) && c.state() == Asr::State::Unloaded, "repeated Stop unload");
    }
    check(stats->destroyed == 1 && !stats->badThread, "backend created/used/destroyed only on worker");
    {
        auto teardown = std::make_shared<Stats>(); teardown->hold = true;
        QElapsedTimer timer; timer.start();
        {
            AsrCoordinator c([teardown] { return std::make_unique<Fake>(teardown); });
            c.loadModel("A"); wait([&] { return c.state() == Asr::State::Ready; });
            c.beginUtterance(); c.pushPcm(chunk(0)); c.finalizeUtterance();
            wait([&] { return teardown->calls == 1; });
        }
        check(teardown->destroyed == 1 && timer.elapsed() < 2000, "destructor cooperative cancel and join");
    }
    std::cout << "ASR coordinator failures=" << failures << '\n'; return failures ? 1 : 0;
}
