#pragma once
#include "audio/AudioInputTypes.h"
#include "asr/AsrTypes.h"

class AsrAudioBuffer
{
public:
    static constexpr int MaximumBytes = 16000 * 30 * 2;
    void reset();
    Asr::Error append(const Audio::PcmChunk &chunk);
    QByteArray snapshot() const { return bytes_; }
    int size() const { return int(bytes_.size()); }
    bool discontinuity() const { return discontinuity_; }
    static std::vector<float> toFloat(const QByteArray &pcm);
private:
    QByteArray bytes_;
    quint64 session_ = 0, sequence_ = 0;
    qint64 timestamp_ = 0;
    bool haveChunk_ = false, discontinuity_ = false;
};
