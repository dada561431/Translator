#include "StreamingProbeCore.h"
#include "asr/AsrAudioBuffer.h"
#include <QCoreApplication>
#include <QtEndian>
#include <atomic>
#include <iostream>
#include <stdexcept>

namespace {
int failures = 0;
void check(bool ok, const char *name) { if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; } }
struct Counters { std::atomic<int> loads{0}, resets{0}, destroyed{0}, accepts{0}; std::thread::id worker; };
class Fake final : public StreamingProbe::IOnlineRecognizer {
public:
    explicit Fake(Counters &c) : c_(c) {}
    ~Fake() override { ++c_.destroyed; }
    QJsonObject load() override { ++c_.loads; c_.worker = std::this_thread::get_id(); return {}; }
    void accept(const std::vector<float> &s) override { ++c_.accepts; available_ += int(s.size()); }
    bool ready() override { return available_ >= 320; }
    void decode() override { available_ -= 320; ++steps_; }
    QString text() override { return QString(steps_, QLatin1Char('a')); }
    bool endpoint() override { return steps_ >= 3; }
    void reset() override { ++c_.resets; steps_ = 0; }
    void finish() override {}
private:
    Counters &c_; int available_ = 0, steps_ = 0;
};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    using namespace StreamingProbe;
    TextStabilizer s;
    auto r = s.update("hello"); check(r.stable.isEmpty() && r.unstable == "hello", "first hypothesis is not committed");
    r = s.update("hello w"); check(r.stable == "hello" && r.added == 2, "English agreement growth");
    r = s.update("hello wo"); check(r.stable == "hello w", "English monotonic prefix");
    r = s.update("hello world"); check(r.stable == "hello wo", "English full growth");
    s.reset(); s.update("I want a"); r = s.update("I want to");
    check(r.stable == "I want " && r.removed == 1 && r.added == 2, "English rewrite counted");
    r = s.update("I want to go"); check(r.stable == "I want to", "English rewrite agreement");
    r = s.update("You want to go"); check(r.stable == "I want to" && r.stableConflict && r.text == "You want to go", "conflict reported without fabricating raw text");
    s.reset(); s.update(QString::fromUtf8("\xe4\xbb\x8a"));
    r = s.update(QString::fromUtf8("\xe4\xbb\x8a\xe5\xa4\xa9")); check(r.commonPrefix == 1 && r.added == 1, "Chinese character growth");
    r = s.update(QString::fromUtf8("\xe4\xbb\x8a\xe5\xa4\xa9\xe4\xb8\x83"));
    r = s.update(QString::fromUtf8("\xe4\xbb\x8a\xe5\xa4\xa9\xe5\xa4\xa9\xe6\xb0\x94"));
    check(r.stable == QString::fromUtf8("\xe4\xbb\x8a\xe5\xa4\xa9") && r.removed == 1 && r.added == 2, "Chinese rewrite preserves agreement");
    s.reset(); s.update(QString::fromUcs4(U"\U0001f600a")); r = s.update(QString::fromUcs4(U"\U0001f601b"));
    check(r.commonPrefix == 0 && r.added == 2 && r.removed == 2, "surrogate pairs are not split");
    QByteArray pcm(6, '\0'); qToLittleEndian<qint16>(-32768, pcm.data()); qToLittleEndian<qint16>(32767, pcm.data() + 2);
    const auto f = AsrAudioBuffer::toFloat(pcm);
    check(f.size() == 3 && f[0] == -1 && f[1] < 1 && f[1] > .999f && f[2] == 0, "existing PCM adapter range");
    check(AsrAudioBuffer::toFloat(QByteArray(1, '\0')).empty(), "odd PCM rejected");
    PcmMailbox q(1280);
    check(q.push(QByteArray(640, 'a')) && q.push(QByteArray(640, 'b')) && !q.push(QByteArray(640, 'c')), "bounded queue rejects overflow");
    check(q.stats().bytes == 1280 && q.stats().dropped == 1 && q.stats().highWaterBytes == 1280, "queue metrics");
    q.close(); QByteArray out;
    check(q.waitPop(out) && out[0] == 'a' && q.waitPop(out) && out[0] == 'b' && !q.waitPop(out), "close drains in order then terminates");
    check(!q.push(QByteArray(640, 'a')), "late PCM rejected after close");
    PcmMailbox gap;
    gap.push(QByteArray(640, '\0'), true); gap.close(); bool discontinuity = false;
    check(gap.waitPop(out, &discontinuity) && discontinuity, "capture gap reaches worker with PCM");
    Counters c; std::vector<QJsonObject> events;
    {
        Worker worker([&] { return std::make_unique<Fake>(c); }, [&](QJsonObject e) { events.push_back(e); }, true);
        check(!worker.push(QByteArray(640, '\0')), "no feed before Start");
        check(worker.start(), "fake model load"); worker.beginAudio();
        check(!worker.push(QByteArray(1, '\0')) && !worker.push(QByteArray(6402, '\0')), "invalid feed rejected");
        for (int i = 0; i < 9; ++i) check(worker.push(QByteArray(640, '\0'), i == 5), "PCM accepted without waiting for SpeechEnd");
        worker.stop(); worker.stop();
        check(!worker.push(QByteArray(640, '\0')) && !worker.failed(), "Stop is idempotent; late feed rejected");
    }
    check(c.loads == 1 && c.resets >= 1 && c.destroyed == 1 && c.worker != std::this_thread::get_id(), "single load, stream resets and worker destruction");
    int partials = 0, finals = 0;
    for (const auto &e : events) { if (e.value("event") == "partial") ++partials; if (e.value("event") == "final") ++finals; }
    check(partials >= 2 && finals >= 2, "incremental fake events and endpoint/manual Final");
    Worker broken([]() -> std::unique_ptr<IOnlineRecognizer> { throw std::runtime_error("test load failure"); }, [](QJsonObject) {}, false);
    check(!broken.start() && broken.failed(), "load failure wakes Start and joins safely"); broken.stop();
    { Worker unbegun([&] { return std::make_unique<Fake>(c); }, [](QJsonObject) {}, false); check(unbegun.start(), "loaded idle"); }
    check(c.destroyed == 2, "Stop before input unblocks worker");
    return failures ? 1 : 0;
}
