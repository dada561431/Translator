#pragma once
#include "audio/IAudioInput.h"
#include "audio/AudioChunkBuffer.h"
#include <QTimer>
#include <functional>
#include <memory>

class AudioInputCoordinator final : public QObject
{
    Q_OBJECT
public:
    using Factory = std::function<std::unique_ptr<IAudioInput>(Audio::InputKind)>;
    explicit AudioInputCoordinator(QObject *parent = nullptr, Factory factory = {});
    ~AudioInputCoordinator() override;
    QList<Audio::DeviceInfo> devices(Audio::InputKind kind) const;
    bool start(Audio::InputKind kind, const QByteArray &deviceId = {});
    void stop();
    Audio::State state() const { return state_; }
    Audio::NativeFormat nativeFormat() const;
    Audio::DeviceInfo selectedDevice() const;
    quint64 droppedChunks() const;
    Audio::Error lastError() const { return error_; }
    quint64 session() const { return generation_; }
signals:
    void stateChanged(Audio::State state);
    void pcmReady(const Audio::PcmChunk &chunk);
    void errorOccurred(const Audio::Error &error);
private:
    void setState(Audio::State state);
    Factory factory_;
    std::unique_ptr<IAudioInput> input_;
    Audio::State state_ = Audio::State::Stopped;
    Audio::Error error_;
    quint64 generation_ = 0;
    AudioChunkBuffer chunks_;
    QTimer delivery_;
};
