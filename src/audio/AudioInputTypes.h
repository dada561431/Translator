#pragma once
#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>
#include <chrono>

namespace Audio {
enum class InputKind { Microphone, SystemLoopback };
enum class State { Stopped, Starting, Running, Stopping, Error };
enum class ErrorCode { None, NoDevice, PermissionDenied, DeviceUnavailable,
                       OpenFailed, UnsupportedFormat, BackendFailure, Overflow };
enum class SampleType { UInt8, Int16, Int24, Int32, Float32 };
struct NativeFormat {
    int sampleRate = 0;
    int channels = 0;
    SampleType sampleType = SampleType::Int16;
    int bytesPerSample() const {
        switch (sampleType) {
        case SampleType::UInt8: return 1;
        case SampleType::Int16: return 2;
        case SampleType::Int24: return 3;
        default: return 4;
        }
    }
    bool valid() const { return sampleRate >= 8000 && sampleRate <= 192000
                               && channels >= 1 && channels <= 8; }
};
inline QString describe(const NativeFormat &f) {
    const char *names[] = {"uint8", "int16", "int24", "int32", "float32"};
    return QStringLiteral("%1 Hz / %2 channels / %3 LE").arg(f.sampleRate)
        .arg(f.channels).arg(QLatin1String(names[int(f.sampleType)]));
}
struct DeviceInfo {
    QByteArray id;
    QString description;
    InputKind kind = InputKind::Microphone;
    bool isDefault = false;
};
struct Error {
    ErrorCode code = ErrorCode::None;
    QString message;
    QString detail;
};
inline constexpr int SampleRate = 16000;
inline constexpr int ChunkSamples = 320;
inline constexpr int ChunkBytes = 640;
struct PcmChunk {
    QByteArray samples; // Always signed int16 little-endian, mono, 16 kHz.
    quint64 session = 0;
    quint64 sequence = 0;
    qint64 timestampUs = 0; // Steady-clock session origin + sample position.
    bool discontinuity = false;
};
inline qint64 monotonicUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
Q_DECLARE_METATYPE(Audio::PcmChunk)
Q_DECLARE_METATYPE(Audio::Error)
Q_DECLARE_METATYPE(Audio::State)
Q_DECLARE_METATYPE(Audio::NativeFormat)
