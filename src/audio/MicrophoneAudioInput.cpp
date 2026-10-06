#include "audio/MicrophoneAudioInput.h"
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QPermissions>
#include <QTimer>

Q_LOGGING_CATEGORY(audioLog, "translator.audio")
MicrophoneAudioInput::MicrophoneAudioInput(QObject *parent) : IAudioInput(parent)
{
    connect(&devices_, &QMediaDevices::audioInputsChanged, this, [this] {
        if (!active_) return;
        bool found = false;
        for (const auto &device : QMediaDevices::audioInputs()) if (device.id() == selectedId_) found = true;
        if (!found || (followsDefault_ && QMediaDevices::defaultAudioInput().id() != selectedId_))
            fail({Audio::ErrorCode::DeviceUnavailable, QStringLiteral("Microphone disconnected or default changed. Start again to select a device."), {}});
    });
}
MicrophoneAudioInput::~MicrophoneAudioInput() { stop(); }
QList<Audio::DeviceInfo> MicrophoneAudioInput::devices() const
{
    QList<Audio::DeviceInfo> result;
    for (const auto &d : QMediaDevices::audioInputs())
        result.append({d.id(), d.description(), Audio::InputKind::Microphone, d.isDefault()});
    return result;
}
void MicrophoneAudioInput::fail(Audio::Error error)
{
    if (source_) { source_->disconnect(this); source_->stop(); }
    active_ = false; stream_ = nullptr;
    qCWarning(audioLog).noquote() << error.message << error.detail;
    emit errorOccurred(error);
}
void MicrophoneAudioInput::start(const QByteArray &id)
{
    if (active_) return;
    stop();
    if (QCoreApplication::instance()->checkPermission(QMicrophonePermission{}) == Qt::PermissionStatus::Denied) {
        fail({Audio::ErrorCode::PermissionDenied, QStringLiteral("Microphone permission denied. Check Windows privacy settings."), {}});
        return;
    }
    QAudioDevice device;
    followsDefault_ = id.isEmpty();
    if (followsDefault_) device = QMediaDevices::defaultAudioInput();
    else for (const auto &d : QMediaDevices::audioInputs()) if (d.id() == id) { device = d; break; }
    if (device.isNull()) {
        fail({id.isEmpty() ? Audio::ErrorCode::NoDevice : Audio::ErrorCode::DeviceUnavailable,
              QStringLiteral("Selected microphone is unavailable."), {}}); return;
    }
    selectedId_ = device.id();
    selectedDevice_ = {device.id(), device.description(), Audio::InputKind::Microphone, device.isDefault()};
    QAudioFormat native = device.preferredFormat();
    format_.sampleRate = native.sampleRate(); format_.channels = native.channelCount();
    switch (native.sampleFormat()) {
    case QAudioFormat::UInt8: format_.sampleType = Audio::SampleType::UInt8; break;
    case QAudioFormat::Int16: format_.sampleType = Audio::SampleType::Int16; break;
    case QAudioFormat::Int32: format_.sampleType = Audio::SampleType::Int32; break;
    case QAudioFormat::Float: format_.sampleType = Audio::SampleType::Float32; break;
    default: format_.sampleRate = 0; break;
    }
    if (!converter_.reset(format_) || !device.isFormatSupported(native)) {
        fail({Audio::ErrorCode::UnsupportedFormat, QStringLiteral("Microphone format is unsupported."), Audio::describe(format_)}); return;
    }
    source_ = std::make_unique<QAudioSource>(device, native);
    source_->setBufferSize(native.bytesForDuration(100000));
    connect(source_.get(), &QAudioSource::stateChanged, this, [this](QtAudio::State state) {
        if (state == QtAudio::StoppedState && active_) {
            const auto code = source_->error();
            fail({code == QtAudio::IOError ? Audio::ErrorCode::DeviceUnavailable : Audio::ErrorCode::OpenFailed,
                  QStringLiteral("Microphone unavailable or could not be opened. Check device access and privacy permissions."),
                  QStringLiteral("QAudioSource error=%1 (Qt may not distinguish Windows permission denial)").arg(int(code))});
        }
    });
    active_ = true;
    stream_ = source_->start();
    if (!active_) return; // A synchronous state error may already have stopped capture.
    if (!stream_ || source_->error() != QtAudio::NoError) {
        fail({Audio::ErrorCode::OpenFailed, QStringLiteral("Microphone could not be opened. Check device and privacy permissions."),
              QStringLiteral("QAudioSource error=%1").arg(int(source_->error()))}); return;
    }
    connect(stream_, &QIODevice::readyRead, this, [this] {
        if (!active_ || !stream_) return;
        while (stream_->bytesAvailable() > 0 && active_) {
            const auto bytes = stream_->read(32768);
            if (bytes.isEmpty()) break;
            converter_.append(bytes, [this](const Audio::PcmChunk &chunk) { if (active_) emit pcmReady(chunk); });
        }
    });
    qCInfo(audioLog).noquote() << "Microphone started" << device.description() << selectedId_.toHex()
                             << Audio::describe(format_) << "-> 16000 Hz / mono / int16 LE";
    emit started();
}
void MicrophoneAudioInput::stop()
{
    const bool wasActive = active_;
    active_ = false; stream_ = nullptr;
    if (source_) { source_->disconnect(this); source_->stop(); source_.reset(); }
    if (wasActive) { qCInfo(audioLog) << "Microphone stopped"; emit stopped(); }
}
