#pragma once
#include "audio/AudioInputTypes.h"
#include <functional>

class PcmConverter
{
public:
    using Consumer = std::function<void(const Audio::PcmChunk &)>;
    bool reset(Audio::NativeFormat format, qint64 originUs = Audio::monotonicUs());
    void append(const QByteArray &bytes, const Consumer &consumer);
    void appendSilence(qsizetype frames, const Consumer &consumer);
    void markDiscontinuity() { discontinuity_ = true; }
    qsizetype bufferedBytes() const { return inputTail_.size() + output_.size(); }
    quint64 outputSamples() const { return outputSamples_; }
private:
    void frame(double mono, const Consumer &consumer);
    void sample(double value, const Consumer &consumer);
    Audio::NativeFormat format_;
    QByteArray inputTail_, output_;
    quint64 frames_ = 0, nextPosition_ = 0, outputSamples_ = 0, sequence_ = 0;
    double previous_ = 0;
    qint64 originUs_ = 0;
    bool discontinuity_ = false;
};
