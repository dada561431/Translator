#include "audio/PcmConverter.h"
#include "audio/AudioChunkBuffer.h"
#include <QCoreApplication>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
int failures = 0;
void check(bool ok, const char *name) { if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; } }
QByteArray ints(const QList<qint16> &values, int frames) {
    QByteArray bytes;
    for (int i = 0; i < frames; ++i) for (auto s : values) { char p[2]; qToLittleEndian(s, p); bytes.append(p, 2); }
    return bytes;
}
QByteArray floats(const QList<float> &values, int frames) {
    QByteArray bytes;
    for (int i = 0; i < frames; ++i) for (auto s : values) {
        quint32 bits; std::memcpy(&bits, &s, 4); char p[4]; qToLittleEndian(bits, p); bytes.append(p, 4);
    }
    return bytes;
}
QByteArray converted(Audio::NativeFormat format, const QByteArray &bytes, int split = 0) {
    PcmConverter c; check(c.reset(format, 1000), "valid native format");
    QByteArray output; quint64 sequence = 0;
    auto consume = [&](const Audio::PcmChunk &chunk) {
        check(chunk.samples.size() == 640, "fixed 640 bytes");
        check(chunk.sequence == sequence && chunk.timestampUs == 1000 + qint64(sequence) * 20000, "monotonic sample timeline");
        ++sequence; output += chunk.samples;
    };
    if (!split) c.append(bytes, consume);
    else for (qsizetype pos = 0; pos < bytes.size(); pos += split) c.append(bytes.mid(pos, split), consume);
    check(c.bufferedBytes() < 640 + 32, "bounded residual input/output");
    return output;
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace Audio;
    for (qint16 value : {qint16(0), qint16(1), qint16(-1), qint16(32767), qint16(-32768), qint16(12345)}) {
        const auto bytes = ints({value}, 320);
        check(converted({16000, 1, SampleType::Int16}, bytes, 7) == bytes, "int16 passthrough bit exact with split sample bytes");
    }
    check(qFromLittleEndian<qint16>(converted({16000, 2, SampleType::Int16}, ints({30000, -10000}, 320)).constData()) == 10000,
          "stereo downmix without overflow");
    const auto silent = converted({48000, 2, SampleType::Float32}, floats({0, 0}, 48000), 17);
    check(silent.size() == 32000 && silent == QByteArray(32000, '\0'), "silence is retained, 48k one second -> exact 16000 samples");
    for (auto value : {1.0f, -1.0f, 3.0f, -3.0f, 0.5f}) {
        const auto output = converted({16000, 1, SampleType::Float32}, floats({value}, 320));
        const auto s = qFromLittleEndian<qint16>(output.constData());
        check(s == (value >= 1 ? 32767 : value <= -1 ? -32768 : 16384), "float clamp and full-scale conversion");
    }
    check(qFromLittleEndian<qint16>(converted({16000, 2, SampleType::Float32}, floats({1.0, 0.0}, 320)).constData()) == 16384,
          "float stereo downmix");
    check(qFromLittleEndian<qint16>(converted({16000, 2, SampleType::Float32}, floats({3.0, -1.0}, 320)).constData()) == 32767,
          "float downmix before clipping");
    check(converted({16000, 1, SampleType::Float32}, floats({std::numeric_limits<float>::quiet_NaN()}, 320)) == QByteArray(640, '\0'),
          "nonfinite float input safely becomes zero");
    check(converted({16000, 1, SampleType::UInt8}, QByteArray(320, char(128))) == QByteArray(640, '\0'), "uint8 midpoint silence");
    QByteArray int24, int32;
    for (int i = 0; i < 320; ++i) {
        int24.append("\0\0\x80", 3);
        char p[4]; qToLittleEndian(std::numeric_limits<qint32>::min(), p); int32.append(p, 4);
    }
    check(qFromLittleEndian<qint16>(converted({16000, 1, SampleType::Int24}, int24).constData()) == -32768, "packed24 sign extension");
    check(qFromLittleEndian<qint16>(converted({16000, 1, SampleType::Int32}, int32).constData()) == -32768, "int32 minimum conversion");
    check(qFromLittleEndian<qint16>(converted({16000, 4, SampleType::Float32}, floats({1, 0, 0, 0}, 320)).constData()) == 8192,
          "explicit four-channel arithmetic mean");
    for (int rate : {8000, 16000, 44100, 48000, 96000}) {
        QByteArray wave;
        for (int i = 0; i < rate; ++i) { char p[2]; qToLittleEndian(qint16(12000 * std::sin(i * 0.1)), p); wave.append(p, 2); }
        const auto whole = converted({rate, 1, SampleType::Int16}, wave);
        check(whole == converted({rate, 1, SampleType::Int16}, wave, 31), "resampling continuity independent of block boundaries");
        check(std::abs(whole.size() / 2 - 16000) <= 320, "one-second sample count with fixed-chunk tail");
        PcmConverter c; c.reset({rate, 1, SampleType::Int16});
        for (int i = 0; i < 60; ++i) c.append(wave, [](const PcmChunk &) {});
        check(std::abs(qint64(c.outputSamples()) - 960000) <= 2, "one-minute rational resampling has no drift");
        check(c.bufferedBytes() < 640, "long converter state bounded");
    }
    PcmConverter c; c.reset({48000, 2, SampleType::Float32});
    QByteArray zeros; c.appendSilence(48000, [&](const PcmChunk &chunk) { zeros += chunk.samples; });
    check(zeros == silent, "WASAPI silent flag conversion equivalent");
    c.reset({16000, 1, SampleType::Int16}); c.markDiscontinuity();
    int gaps = 0; c.append(ints({0}, 640), [&](const PcmChunk &chunk) { gaps += chunk.discontinuity; });
    check(gaps == 1, "discontinuity marked once");
    check(!c.reset({0, 1, SampleType::Int16}) && !c.reset({48000, 9, SampleType::Int16}), "unsupported format rejected");
    AudioChunkBuffer queue;
    for (int i = 0; i < 10000; ++i) queue.push({QByteArray(640, '\0'), 0, quint64(i), i * 20000, false});
    check(queue.size() == 50 && queue.dropped() == 9950, "slow-consumer buffer bounded at 50 chunks");
    auto retained = queue.take(50);
    check(retained.front().sequence == 9950 && retained.front().discontinuity, "overflow retains latest and identifies gap");
    queue.clear(); check(queue.size() == 0 && queue.dropped() == 0, "new session clears bounded delivery state");
    std::cout << "PCM converter failures=" << failures << '\n';
    return failures ? 1 : 0;
}
