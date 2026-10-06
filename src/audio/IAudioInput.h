#pragma once
#include <QObject>
#include "audio/AudioInputTypes.h"

class IAudioInput : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual QList<Audio::DeviceInfo> devices() const = 0;
    // Empty ID follows the default selected at Start. An explicit ID remains pinned.
    virtual void start(const QByteArray &deviceId) = 0;
    virtual void stop() = 0; // Synchronous resource teardown; safe when already stopped.
    virtual bool running() const = 0;
    virtual Audio::NativeFormat nativeFormat() const = 0;
    virtual Audio::DeviceInfo selectedDevice() const = 0;
    virtual quint64 droppedChunks() const { return 0; }
signals:
    void pcmReady(const Audio::PcmChunk &chunk);
    void started();
    void stopped();
    void errorOccurred(const Audio::Error &error);
};
