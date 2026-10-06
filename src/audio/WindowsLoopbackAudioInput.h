#pragma once
#include "audio/IAudioInput.h"
#include <memory>

class WindowsLoopbackAudioInput final : public IAudioInput
{
    Q_OBJECT
public:
    explicit WindowsLoopbackAudioInput(QObject *parent = nullptr);
    ~WindowsLoopbackAudioInput() override;
    QList<Audio::DeviceInfo> devices() const override;
    void start(const QByteArray &id) override;
    void stop() override;
    bool running() const override;
    Audio::NativeFormat nativeFormat() const override;
    quint64 droppedChunks() const override;
    Audio::DeviceInfo selectedDevice() const override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void drain();
};
