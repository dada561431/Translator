#pragma once
#include "audio/IAudioInput.h"
#include "audio/PcmConverter.h"
#include <QMediaDevices>
#include <QAudioSource>
#include <memory>

class MicrophoneAudioInput final : public IAudioInput
{
    Q_OBJECT
public:
    explicit MicrophoneAudioInput(QObject *parent = nullptr);
    ~MicrophoneAudioInput() override;
    QList<Audio::DeviceInfo> devices() const override;
    void start(const QByteArray &id) override;
    void stop() override;
    bool running() const override { return active_; }
    Audio::NativeFormat nativeFormat() const override { return format_; }
    Audio::DeviceInfo selectedDevice() const override { return selectedDevice_; }
private:
    void fail(Audio::Error error);
    QMediaDevices devices_;
    std::unique_ptr<QAudioSource> source_;
    QIODevice *stream_ = nullptr;
    PcmConverter converter_;
    Audio::NativeFormat format_;
    QByteArray selectedId_;
    Audio::DeviceInfo selectedDevice_;
    bool followsDefault_ = false, active_ = false;
};
