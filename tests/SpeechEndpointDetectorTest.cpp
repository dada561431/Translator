#include "audio/SpeechEndpointDetector.h"
#include <QtEndian>
#include <iostream>
#include <limits>

namespace {
int failures = 0;
void check(bool ok, const char *name) { if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; } }
struct Harness {
    SpeechEndpointDetector detector;
    quint64 seq = 0, session = 1;
    int starts = 0;
    QList<SpeechEndpointDetector::Utterance> ends;
    SpeechEndpointDetector::Observation feed(qint16 value, bool discontinuity = false) {
        QByteArray pcm(Audio::ChunkBytes, '\0');
        for (int i = 0; i < 320; ++i) qToLittleEndian<qint16>(i % 2 ? value : -value, pcm.data() + i * 2);
        auto out = detector.consume({pcm, session, seq, qint64(seq) * 20000, discontinuity}); ++seq;
        if (out.started) ++starts;
        if (out.ended) ends.append(std::move(*out.ended));
        return out;
    }
    void feed(int n, qint16 value) { while (n--) feed(value); }
};
}
int main()
{
    SpeechEndpointConfig config;
    check(config.valid(), "default config valid");
    for (int i = 0; i < 6; ++i) {
        auto bad = config;
        if (i == 0) bad.preRollMs = -1;
        if (i == 1) bad.preRollMs = 2000;
        if (i == 2) bad.trailingSilenceMs = 13000;
        if (i == 3) bad.noiseRatio = 1;
        if (i == 4) bad.minimumRms = std::numeric_limits<double>::quiet_NaN();
        if (i == 5) bad.startMs = 0;
        Harness h; check(!h.detector.configure(bad), "reject invalid config");
    }
    Harness silence; silence.feed(500, 0);
    check(silence.starts == 0 && silence.ends.isEmpty() && silence.detector.bufferedChunks() == 15, "10 seconds silence bounded/no utterance");
    Harness click; click.feed(15, 0); click.feed(2, 20000); click.feed(100, 0);
    check(click.starts == 0 && click.ends.isEmpty(), "40ms click rejected");
    Harness shortNoise; shortNoise.feed(5, 1000); shortNoise.feed(30, 0);
    check(shortNoise.ends.isEmpty(), "minimum speech rejects 100ms burst");
    Harness word; word.feed(20, 1); word.feed(15, 1000); word.feed(29, 0);
    check(word.ends.isEmpty(), "580ms silence not sealed");
    check(!word.detector.advanceTime(700000 + 599000), "599ms not sealed");
    word.feed(1, 0);
    check(word.starts == 1 && word.ends.size() == 1, "300ms word sealed once after 600ms");
    if (!word.ends.isEmpty()) {
        const auto &u = word.ends.front();
        check(u.chunks.size() == 60 && u.chunks.front().sequence == 5, "300ms pre-roll plus all candidate and silence PCM");
        check(u.speechEndUs == 700000 && !u.forced, "last active chunk end timestamp");
        check(qFromLittleEndian<qint16>(u.chunks.at(15).samples.constData()) == -1000, "original PCM preserved");
    }
    word.feed(100, 0); check(word.ends.size() == 1, "no repeated silence Final");
    Harness max; max.feed(1250, 1000);
    check(max.ends.size() == 2 && max.ends.front().forced && max.ends.front().chunks.size() == 600
        && max.ends.at(1).chunks.front().sequence == 600 && max.detector.bufferedChunks() == 50, "12s force cap continuous successor without overlap");
    Harness reset; reset.feed(30, 1000); reset.detector.reset();
    check(reset.detector.state() == SpeechEndpointDetector::State::Idle && reset.detector.bufferedChunks() == 0, "Stop cancel and reset");
    reset.feed(30, 0); check(reset.ends.isEmpty(), "no cancelled speech delivered");
    Harness sessions; sessions.feed(30, 1000); ++sessions.session; auto changed = sessions.feed(qint16(0));
    sessions.feed(40, 0); check(changed.reset && sessions.ends.isEmpty(), "new session discards previous speech");
    Harness disc; disc.feed(15, 1000); disc.feed(qint16(1000), true); disc.feed(30, 0);
    check(disc.ends.size() == 1 && disc.ends.front().chunks.at(15).discontinuity, "discontinuity metadata retained/no padding");
    Harness gap; gap.feed(15, 1000);
    check(!gap.detector.advanceTime(899000), "missing packets before end threshold");
    check(bool(gap.detector.advanceTime(900000)) && !gap.detector.advanceTime(1500000), "packet silence seals once without synthetic PCM");
    SpeechEndpointDetector clock;
    for (int i = 0; i < 15; ++i) {
        QByteArray pcm(640, '\0');
        for (int j = 0; j < 320; ++j) qToLittleEndian<qint16>(1000, pcm.data() + j * 2);
        clock.consume({pcm, 1, quint64(i), qint64(i) * 20000, false}, 1000000 + i * 20000);
    }
    check(bool(clock.advanceTime(1900000)), "wall clock packet pause seals");
    QByteArray resumed(640, '\0');
    const auto anchored = clock.consume({resumed, 1, 15, 300000, true}, 5000000);
    check(anchored.chunkEndUs == 5000000, "sample clock re-anchored after loopback pause");
    Harness stalePreRoll; stalePreRoll.feed(15, 0); stalePreRoll.detector.advanceTime(900000);
    check(stalePreRoll.detector.bufferedChunks() == 0, "pre-roll expires across absent-packet gap");
    Harness staleCandidate; staleCandidate.feed(2, 1000); staleCandidate.detector.advanceTime(640000);
    check(staleCandidate.detector.state() == SpeechEndpointDetector::State::Idle && staleCandidate.detector.bufferedChunks() == 0,
          "unconfirmed click expires across absent packets");
    Harness low; low.feed(200, 2); const auto lowObs = low.feed(qint16(2));
    check(lowObs.noiseFloor > config.minimumNoiseFloor && low.starts == 0, "idle low background EMA");
    Harness high;
    // A slowly changing background can be learned; abrupt music/noise is a documented limitation.
    for (int level = 2; level <= 60; ++level) high.feed(200, qint16(level));
    const auto highObs = high.feed(qint16(60));
    check(highObs.noiseFloor > lowObs.noiseFloor * 10 && high.starts == 0, "adaptive rising background");
    high.feed(20, 500); high.feed(40, 60);
    check(high.ends.size() == 1, "relative speech over higher learned noise floor");
    Harness hysteresis; hysteresis.feed(15, 1000);
    for (int i = 0; i < 50; ++i) hysteresis.feed(qint16(i % 2 ? 80 : 50));
    check(hysteresis.starts == 1 && hysteresis.ends.isEmpty(), "hysteresis keeps active near start threshold");
    hysteresis.feed(30, 0); check(hysteresis.ends.size() == 1, "hysteresis ends only sustained silence");
    std::cout << "Speech endpoint failures=" << failures << '\n'; return failures ? 1 : 0;
}
