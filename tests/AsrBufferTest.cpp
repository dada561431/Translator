#include "asr/AsrAudioBuffer.h"
#include <QtEndian>
#include <cmath>
#include <iostream>
#include <random>

int main()
{
    int failures = 0;
    auto check = [&](bool value, const char *name) { if (!value) { ++failures; std::cerr << name << '\n'; } };
    QByteArray bytes;
    std::mt19937 random(8);
    QList<qint16> values{0, 32767, -32768, 1, -1};
    for (int i = 0; i < 1000; ++i) values.append(qint16(random()));
    for (auto value : values) { char b[2]; qToLittleEndian(value, b); bytes.append(b, 2); }
    const auto floats = AsrAudioBuffer::toFloat(bytes);
    check(floats.size() == size_t(values.size()), "sample count");
    for (int i = 0; i < values.size(); ++i)
        check(floats[size_t(i)] == float(values[i]) / 32768.0f, "little-endian signed conversion");
    check(floats[0] == 0 && floats[1] < 1 && floats[1] > .9999 && floats[2] == -1, "full scale");
    check(AsrAudioBuffer::toFloat(QByteArray(1, 'x')).empty(), "odd PCM rejected");
    AsrAudioBuffer buffer;
    auto chunk = [](quint64 sequence, quint64 session = 1, bool gap = false) {
        return Audio::PcmChunk{QByteArray(640, '\0'), session, sequence, qint64(sequence) * 20000, gap};
    };
    check(buffer.append(chunk(0)).code == Asr::ErrorCode::None, "first chunk");
    const auto snapshot = buffer.snapshot();
    check(buffer.append(chunk(1)).code == Asr::ErrorCode::None && snapshot.size() == 640, "immutable snapshot");
    check(buffer.append(chunk(1)).code == Asr::ErrorCode::InvalidAudio, "duplicate rejected");
    check(buffer.append(chunk(0)).code == Asr::ErrorCode::InvalidAudio, "old sequence rejected");
    auto staleTimestamp = chunk(2); staleTimestamp.timestampUs = 20000;
    check(buffer.append(staleTimestamp).code == Asr::ErrorCode::InvalidAudio, "old timestamp rejected");
    check(buffer.append(chunk(2, 2)).code == Asr::ErrorCode::InvalidAudio, "audio session cannot mix");
    check(buffer.append(chunk(3)).code == Asr::ErrorCode::None && buffer.discontinuity(), "sequence gap flagged");
    buffer.reset(); check(!buffer.discontinuity() && !buffer.size(), "reset");
    check(buffer.append(chunk(0, 1, true)).code == Asr::ErrorCode::None && buffer.discontinuity(), "explicit discontinuity");
    buffer.reset();
    for (int i = 0; i < 1500; ++i) check(buffer.append(chunk(quint64(i))).code == Asr::ErrorCode::None, "30 seconds accepted");
    check(buffer.size() == 960000, "maximum bound");
    check(buffer.append(chunk(1500)).code == Asr::ErrorCode::UtteranceTooLong && buffer.size() == 960000, "overflow keeps complete old audio");
    auto invalid = chunk(1500); invalid.samples.resize(639);
    check(buffer.append(invalid).code == Asr::ErrorCode::InvalidAudio, "wrong chunk size");
    std::cout << "ASR buffer failures=" << failures << '\n'; return failures ? 1 : 0;
}
