#include "asr/AsrAudioBuffer.h"
#include <QtEndian>

void AsrAudioBuffer::reset()
{
    bytes_.clear(); session_ = sequence_ = 0; timestamp_ = 0;
    haveChunk_ = discontinuity_ = false;
}
Asr::Error AsrAudioBuffer::append(const Audio::PcmChunk &chunk)
{
    using Asr::ErrorCode;
    if (chunk.samples.size() != Audio::ChunkBytes || chunk.timestampUs < 0)
        return {ErrorCode::InvalidAudio, QStringLiteral("Expected 640 bytes of 16 kHz mono int16 LE PCM and a monotonic timestamp.")};
    if (haveChunk_ && chunk.session != session_)
        return {ErrorCode::InvalidAudio, QStringLiteral("Audio session changed; cancel and begin a new utterance.")};
    if (haveChunk_ && (chunk.sequence <= sequence_ || chunk.timestampUs <= timestamp_))
        return {ErrorCode::InvalidAudio, QStringLiteral("Duplicate or out-of-order PCM rejected.")};
    if (bytes_.size() + chunk.samples.size() > MaximumBytes)
        return {ErrorCode::UtteranceTooLong, QStringLiteral("Utterance exceeds 30 seconds. Finalize or cancel before more PCM.")};
    discontinuity_ |= chunk.discontinuity || (haveChunk_ &&
        (chunk.sequence != sequence_ + 1 || chunk.timestampUs - timestamp_ != 20000));
    bytes_.append(chunk.samples); session_ = chunk.session; sequence_ = chunk.sequence;
    timestamp_ = chunk.timestampUs; haveChunk_ = true;
    return {};
}
std::vector<float> AsrAudioBuffer::toFloat(const QByteArray &pcm)
{
    if (pcm.size() % 2) return {};
    std::vector<float> result(size_t(pcm.size() / 2));
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = qFromLittleEndian<qint16>(pcm.constData() + i * 2) / 32768.0f;
    return result;
}
