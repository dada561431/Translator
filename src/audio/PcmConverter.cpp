#include "audio/PcmConverter.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
double decode(const char *p, Audio::SampleType type)
{
    switch (type) {
    case Audio::SampleType::UInt8: return (quint8(*p) - 128) / 128.0;
    case Audio::SampleType::Int16: {
        const auto s = qFromLittleEndian<qint16>(p);
        return s / (s < 0 ? 32768.0 : 32767.0);
    }
    case Audio::SampleType::Int24: {
        qint32 s = quint8(p[0]) | (quint32(quint8(p[1])) << 8) | (quint32(quint8(p[2])) << 16);
        if (s & 0x800000) s |= qint32(0xff000000);
        return s / (s < 0 ? 8388608.0 : 8388607.0);
    }
    case Audio::SampleType::Int32: {
        const auto s = qFromLittleEndian<qint32>(p);
        return s / (s < 0 ? 2147483648.0 : 2147483647.0);
    }
    case Audio::SampleType::Float32: {
        quint32 bits = qFromLittleEndian<quint32>(p);
        float s;
        std::memcpy(&s, &bits, sizeof(s));
        return std::isfinite(s) ? double(s) : 0.0; // Downmix first; clamp unified output.
    }
    }
    return 0;
}
}
bool PcmConverter::reset(Audio::NativeFormat format, qint64 originUs)
{
    format_ = format;
    inputTail_.clear(); output_.clear();
    frames_ = nextPosition_ = outputSamples_ = sequence_ = 0;
    previous_ = 0; originUs_ = originUs; discontinuity_ = false;
    return format.valid();
}
void PcmConverter::append(const QByteArray &bytes, const Consumer &consumer)
{
    if (!format_.valid()) return;
    const int stride = format_.channels * format_.bytesPerSample();
    // At most one incomplete native frame survives between reads.
    const QByteArray input = inputTail_ + bytes;
    const qsizetype complete = input.size() / stride * stride;
    for (qsizetype i = 0; i < complete; i += stride) {
        double mono = 0;
        for (int c = 0; c < format_.channels; ++c)
            mono += decode(input.constData() + i + c * format_.bytesPerSample(), format_.sampleType);
        frame(mono / format_.channels, consumer);
    }
    inputTail_ = input.mid(complete);
}
void PcmConverter::appendSilence(qsizetype frames, const Consumer &consumer)
{
    if (!format_.valid()) return;
    for (qsizetype i = 0; i < frames; ++i) frame(0, consumer);
}
void PcmConverter::frame(double mono, const Consumer &consumer)
{
    // Integer rational positions avoid cumulative drift; retain the preceding frame
    // so interpolation is identical regardless of native read boundaries.
    const quint64 position = frames_ * Audio::SampleRate;
    while (nextPosition_ <= position) {
        const double alpha = frames_ == 0 ? 1.0
            : double(nextPosition_ - (frames_ - 1) * Audio::SampleRate) / Audio::SampleRate;
        sample(previous_ + (mono - previous_) * alpha, consumer);
        nextPosition_ += quint64(format_.sampleRate);
    }
    previous_ = mono;
    ++frames_;
}
void PcmConverter::sample(double value, const Consumer &consumer)
{
    value = std::clamp(value, -1.0, 1.0);
    const qint16 s = qint16(std::lround(value * (value < 0 ? 32768.0 : 32767.0)));
    char bytes[2]; qToLittleEndian(s, bytes);
    output_.append(bytes, 2);
    ++outputSamples_;
    if (output_.size() == Audio::ChunkBytes) {
        Audio::PcmChunk chunk{output_, 0, sequence_, originUs_ + qint64(sequence_) * 20000, discontinuity_};
        ++sequence_; discontinuity_ = false; output_.clear();
        consumer(chunk);
    }
}
