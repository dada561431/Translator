#include "audio/AudioInputCoordinator.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <iostream>

namespace {
int failures = 0;
void check(bool ok, const char *name) { if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; } }
struct Fake : IAudioInput {
    using IAudioInput::IAudioInput;
    QList<Audio::DeviceInfo> available;
    bool active = false;
    int *released = nullptr;
    ~Fake() override { stop(); if (released) ++*released; }
    QList<Audio::DeviceInfo> devices() const override { return available; }
    void start(const QByteArray &id) override {
        bool found = false;
        for (const auto &d : available) if ((id.isEmpty() && d.isDefault) || id == d.id) found = true;
        if (!found) { emit errorOccurred({Audio::ErrorCode::NoDevice, QStringLiteral("No device"), {}}); return; }
        active = true; emit started();
    }
    void stop() override { if (active) { active = false; emit stopped(); } }
    bool running() const override { return active; }
    Audio::NativeFormat nativeFormat() const override { return {48000, 2, Audio::SampleType::Float32}; }
    Audio::DeviceInfo selectedDevice() const override { return available.isEmpty() ? Audio::DeviceInfo{} : available.front(); }
};
void pump() { QCoreApplication::sendPostedEvents(); QCoreApplication::processEvents(QEventLoop::AllEvents); }
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace Audio;
    Fake *last = nullptr; int released = 0; QList<DeviceInfo> available;
    AudioInputCoordinator::Factory factory = [&](InputKind kind) {
        auto f = std::make_unique<Fake>(); f->available = available;
        for (auto &d : f->available) d.kind = kind;
        f->released = &released; last = f.get(); return f;
    };
    AudioInputCoordinator coordinator(nullptr, factory);
    check(coordinator.state() == State::Stopped && !last, "privacy: construction opens no source");
    coordinator.stop(); coordinator.stop(); check(coordinator.state() == State::Stopped, "repeated Stop no-op");
    check(coordinator.devices(InputKind::Microphone).isEmpty(), "zero devices");
    coordinator.start(InputKind::Microphone); pump();
    check(coordinator.state() == State::Error && coordinator.lastError().code == ErrorCode::NoDevice, "no device safe error");
    available = {{"one", QStringLiteral("Default microphone"), InputKind::Microphone, true}};
    check(coordinator.devices(InputKind::Microphone).size() == 1, "one default device");
    available = {{"one", QStringLiteral("Duplicate name"), InputKind::Microphone, true},
                 {"two", QStringLiteral("Duplicate name"), InputKind::Microphone, false}};
    auto devices = coordinator.devices(InputKind::SystemLoopback);
    check(devices.size() == 2 && devices[0].isDefault && devices[1].id != devices[0].id
          && devices[1].kind == InputKind::SystemLoopback, "stable IDs distinguish names and source kinds");
    int chunks = 0; quint64 lastSession = 0;
    QObject::connect(&coordinator, &AudioInputCoordinator::pcmReady, [&](const PcmChunk &chunk) {
        ++chunks; lastSession = chunk.session;
    });
    for (InputKind kind : {InputKind::Microphone, InputKind::SystemLoopback}) {
        for (int i = 0; i < 20; ++i) {
            check(coordinator.start(kind, "two"), "start from stopped/error");
            const auto before = last;
            check(!coordinator.start(kind, "one") && last == before, "repeated Start during Starting rejected");
            pump(); check(coordinator.state() == State::Running, "Running after started");
            check(!coordinator.start(kind == InputKind::Microphone ? InputKind::SystemLoopback : InputKind::Microphone), "source switching requires Stop");
            emit last->pcmReady({QByteArray(640, '\0'), 0, 0, 1000, false});
            { QEventLoop delivery; QTimer::singleShot(30, &delivery, &QEventLoop::quit); delivery.exec(); }
            check(lastSession == coordinator.session(), "chunk tagged with session");
            emit last->errorOccurred({ErrorCode::DeviceUnavailable, QStringLiteral("Disconnected/default changed"), {}});
            coordinator.stop(); // Old queued error must not corrupt a new Start.
            coordinator.start(kind, "one"); pump();
            check(coordinator.state() == State::Running, "stale error suppressed after Stop/Start");
            coordinator.stop(); coordinator.stop(); pump();
            check(coordinator.state() == State::Stopped, "repeated teardown deterministic");
        }
    }
    coordinator.start(InputKind::Microphone); pump();
    emit last->errorOccurred({ErrorCode::DeviceUnavailable, QStringLiteral("Device removed"), {}}); pump();
    check(coordinator.state() == State::Error && coordinator.lastError().code == ErrorCode::DeviceUnavailable, "device loss stops/releases and preserves error");
    available[0].isDefault = false; available[1].isDefault = true;
    check(coordinator.devices(InputKind::Microphone)[1].isDefault, "default-device provider refresh");
    coordinator.start(InputKind::Microphone); pump();
    emit last->stopped(); pump(); check(coordinator.state() == State::Stopped, "unexpected stopped restores state");
    for (auto code : {ErrorCode::PermissionDenied, ErrorCode::UnsupportedFormat, ErrorCode::OpenFailed, ErrorCode::BackendFailure}) {
        coordinator.start(InputKind::Microphone); pump();
        emit last->errorOccurred({code, QStringLiteral("Explicit error"), {}}); pump();
        check(coordinator.state() == State::Error && coordinator.lastError().code == code, "error categories retained and restartable");
    }
    const int before = released;
    { AudioInputCoordinator scoped(nullptr, factory); scoped.start(InputKind::SystemLoopback); pump(); }
    check(released == before + 1 && chunks == 40, "destruction releases active backend, no queued stale chunks");
    {
        AudioInputCoordinator consumerStops(nullptr, factory);
        consumerStops.start(InputKind::Microphone); pump();
        QObject::connect(&consumerStops, &AudioInputCoordinator::pcmReady, &consumerStops, [&] { consumerStops.stop(); });
        emit last->pcmReady({QByteArray(640, '\0'), 0, 0, 1000, false});
        emit last->pcmReady({QByteArray(640, '\0'), 0, 1, 21000, false});
        QEventLoop delivery; QTimer::singleShot(30, &delivery, &QEventLoop::quit); delivery.exec();
        check(consumerStops.state() == State::Stopped, "PCM consumer can synchronously Stop without destroying emitting backend stack");
    }
    AudioInputCoordinator nullBackend(nullptr, [](InputKind) -> std::unique_ptr<IAudioInput> { return {}; });
    check(!nullBackend.start(InputKind::Microphone) && nullBackend.state() == State::Error, "missing backend handled");
    std::cout << "Coordinator failures=" << failures << '\n';
    return failures ? 1 : 0;
}
