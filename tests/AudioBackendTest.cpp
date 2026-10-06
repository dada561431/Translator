#include "audio/WindowsLoopbackAudioInput.h"
#include "audio/AudioInputCoordinator.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <iostream>

namespace {
int failures = 0;
void check(bool ok, const char *message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // Invalid ID never opens a hardware endpoint; this regression needs no audio
    // device, microphone permission, playback, or recording on the test machine.
    WindowsLoopbackAudioInput input;
    check(!input.running(), "construction does not capture");
    int stopped = 0, errors = 0;
    QEventLoop wait;
    QObject::connect(&input, &IAudioInput::stopped, [&] { ++stopped; });
    QObject::connect(&input, &IAudioInput::errorOccurred, [&](const Audio::Error &error) {
        ++errors; check(error.code != Audio::ErrorCode::None, "explicit failure category");
        check(!input.running(), "fatal error only delivered after native teardown");
        wait.quit();
    });
    QTimer::singleShot(3000, &wait, &QEventLoop::quit);
    input.start("phase8a-not-a-valid-endpoint-id");
#ifdef Q_OS_WIN
    wait.exec();
#endif
    check(errors == 1 && stopped == 0, "fatal Error not preceded by ordinary Stopped");
    input.stop(); input.stop();
#ifdef Q_OS_WIN
    for (int i = 0; i < 20; ++i) {
        input.start("phase8a-not-a-valid-endpoint-id"); input.stop(); input.stop();
    }
    check(!input.running(), "Stop during native startup joins repeatedly");
#endif
    AudioInputCoordinator coordinator;
    QEventLoop coordinatorWait;
    QObject::connect(&coordinator, &AudioInputCoordinator::errorOccurred, &coordinatorWait, &QEventLoop::quit);
    QTimer::singleShot(3000, &coordinatorWait, &QEventLoop::quit);
    coordinator.start(Audio::InputKind::SystemLoopback, "phase8a-not-a-valid-endpoint-id");
    coordinatorWait.exec();
    check(coordinator.state() == Audio::State::Error
          && coordinator.lastError().code != Audio::ErrorCode::None,
          "real adapter error survives coordinator session retirement");
    coordinator.stop();
    check(coordinator.state() == Audio::State::Stopped, "error-state Stop safe");
    std::cout << "Backend failure/cleanup failures=" << failures << '\n';
    return failures ? 1 : 0;
}
