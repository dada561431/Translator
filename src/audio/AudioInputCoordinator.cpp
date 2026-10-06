#include "audio/AudioInputCoordinator.h"
#include "audio/MicrophoneAudioInput.h"
#include "audio/WindowsLoopbackAudioInput.h"
#include <QThread>

AudioInputCoordinator::AudioInputCoordinator(QObject *parent, Factory factory)
    : QObject(parent), factory_(std::move(factory))
{
    qRegisterMetaType<Audio::PcmChunk>();
    qRegisterMetaType<Audio::Error>();
    if (!factory_) factory_ = [](Audio::InputKind kind) -> std::unique_ptr<IAudioInput> {
        if (kind == Audio::InputKind::Microphone) return std::make_unique<MicrophoneAudioInput>();
        return std::make_unique<WindowsLoopbackAudioInput>();
    };
    delivery_.setInterval(20);
    connect(&delivery_, &QTimer::timeout, this, [this] {
        if (state_ != Audio::State::Running) return;
        for (const auto &chunk : chunks_.take()) {
            if (state_ != Audio::State::Running || chunk.session != generation_) break;
            emit pcmReady(chunk);
        }
    });
}
AudioInputCoordinator::~AudioInputCoordinator() { stop(); }
QList<Audio::DeviceInfo> AudioInputCoordinator::devices(Audio::InputKind kind) const
{
    const auto backend = factory_(kind);
    return backend ? backend->devices() : QList<Audio::DeviceInfo>{};
}
Audio::NativeFormat AudioInputCoordinator::nativeFormat() const
{
    return input_ ? input_->nativeFormat() : Audio::NativeFormat{};
}
Audio::DeviceInfo AudioInputCoordinator::selectedDevice() const
{
    return input_ ? input_->selectedDevice() : Audio::DeviceInfo{};
}
quint64 AudioInputCoordinator::droppedChunks() const
{
    return chunks_.dropped() + (input_ ? input_->droppedChunks() : 0);
}
void AudioInputCoordinator::setState(Audio::State state)
{
    if (state_ == state) return;
    state_ = state; emit stateChanged(state);
}
bool AudioInputCoordinator::start(Audio::InputKind kind, const QByteArray &deviceId)
{
    Q_ASSERT(thread() == QThread::currentThread());
    // Changing source/device requires explicit Stop, including during Starting.
    if (state_ != Audio::State::Stopped && state_ != Audio::State::Error) return false;
    error_ = {}; chunks_.clear(); const auto generation = ++generation_;
    input_ = factory_(kind);
    setState(Audio::State::Starting);
    if (generation != generation_ || state_ != Audio::State::Starting) return false;
    if (!input_) {
        error_ = {Audio::ErrorCode::BackendFailure, QStringLiteral("Audio backend unavailable."), {}};
        setState(Audio::State::Error); emit errorOccurred(error_); return false;
    }
    connect(input_.get(), &IAudioInput::started, this, [this, generation] {
        if (generation == generation_ && state_ == Audio::State::Starting) {
            delivery_.start(); setState(Audio::State::Running);
        }
    }, Qt::QueuedConnection);
    connect(input_.get(), &IAudioInput::pcmReady, this, [this, generation](Audio::PcmChunk chunk) {
        if (generation != generation_ || (state_ != Audio::State::Running && state_ != Audio::State::Starting)) return;
        chunk.session = generation; chunks_.push(std::move(chunk));
    });
    connect(input_.get(), &IAudioInput::errorOccurred, this, [this, generation](const Audio::Error &error) {
        if (generation != generation_ || !input_) return;
        ++generation_; delivery_.stop(); chunks_.clear(); input_->stop(); input_.reset(); error_ = error;
        setState(Audio::State::Error); emit errorOccurred(error_);
    }, Qt::QueuedConnection);
    connect(input_.get(), &IAudioInput::stopped, this, [this, generation] {
        if (generation == generation_ && (state_ == Audio::State::Running || state_ == Audio::State::Starting)) {
            ++generation_; delivery_.stop(); chunks_.clear(); input_->stop(); input_.reset(); setState(Audio::State::Stopped);
        }
    }, Qt::QueuedConnection);
    input_->start(deviceId);
    return true;
}
void AudioInputCoordinator::stop()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (state_ == Audio::State::Stopped || state_ == Audio::State::Stopping) return;
    ++generation_; setState(Audio::State::Stopping);
    delivery_.stop(); chunks_.clear();
    if (input_) { input_->stop(); input_.reset(); }
    setState(Audio::State::Stopped);
}
