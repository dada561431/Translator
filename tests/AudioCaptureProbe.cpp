#include "audio/AudioInputCoordinator.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <iostream>

namespace {
// Explicit QA-only output fixture: two seconds silence, three seconds tone,
// then two seconds silence. Never microphone input or a recording file.
class TestTone final : public QIODevice {
public:
    explicit TestTone(QAudioFormat format) : format_(format) { open(ReadOnly); }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return format_.bytesForDuration(100000) + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 length) override {
        const int stride = format_.bytesPerFrame();
        const qint64 frames = length / stride;
        for (qint64 i = 0; i < frames; ++i, ++position_) {
            double t = double(position_) / format_.sampleRate();
            double phase = std::fmod(t, 7.0);
            float s = phase >= 2 && phase < 5 ? float(0.2 * std::sin(t * 440 * 6.283185307179586)) : 0;
            for (int c = 0; c < format_.channelCount(); ++c) {
                char *p = data + i * stride + c * format_.bytesPerSample();
                switch (format_.sampleFormat()) {
                case QAudioFormat::Int16: qToLittleEndian(qint16(s * 32767), p); break;
                case QAudioFormat::Int32: qToLittleEndian(qint32(s * 2147483647.0), p); break;
                case QAudioFormat::Float: { quint32 bits; std::memcpy(&bits, &s, 4); qToLittleEndian(bits, p); break; }
                case QAudioFormat::UInt8: *p = char(128 + int(s * 127)); break;
                default: return -1;
                }
            }
        }
        return frames * stride;
    }
    qint64 writeData(const char *, qint64) override { return -1; }
private:
    QAudioFormat format_;
    quint64 position_ = 0;
};
void print(const QJsonObject &object) {
    std::cout << QJsonDocument(object).toJson(QJsonDocument::Compact).constData() << std::endl;
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Phase 8A local audio QA. No recording, ASR or network."));
    parser.addHelpOption();
    parser.addOptions({{"list", "Enumerate only; never opens capture."},
        {"kind", "microphone or loopback", "kind"},
        {"device", "Stable device ID: hex for microphone, UTF-8 WASAPI endpoint for loopback; omitted = default", "id"},
        {"seconds", "Capture duration per cycle (1-600)", "n", "7"},
        {"cycles", "Number of explicit Start/Stop cycles (1-100)", "n", "1"},
        {"exit-while-running", "QA: quit directly during capture instead of an explicit Stop."},
        {"test-tone", "Explicitly play QA silence/tone on default system output (loopback only)."}});
    parser.process(app);
    AudioInputCoordinator coordinator;
    if (parser.isSet("list") || !parser.isSet("kind")) {
        for (auto kind : {Audio::InputKind::Microphone, Audio::InputKind::SystemLoopback})
            for (const auto &d : coordinator.devices(kind))
                print({{"kind", kind == Audio::InputKind::Microphone ? "microphone" : "loopback"},
                    {"id", kind == Audio::InputKind::Microphone ? QString::fromLatin1(d.id.toHex()) : QString::fromUtf8(d.id)},
                    {"description", d.description}, {"default", d.isDefault}});
        return 0;
    }
    const auto name = parser.value("kind");
    if (name != "microphone" && name != "loopback") return 2;
    const auto kind = name == "microphone" ? Audio::InputKind::Microphone : Audio::InputKind::SystemLoopback;
    const QByteArray id = kind == Audio::InputKind::Microphone
        ? QByteArray::fromHex(parser.value("device").toLatin1()) : parser.value("device").toUtf8();
    if (parser.isSet("device") && (id.isEmpty() || (kind == Audio::InputKind::Microphone
        && id.toHex() != parser.value("device").toLatin1().toLower()))) return 2;
    bool okSeconds = false, okCycles = false;
    const int seconds = parser.value("seconds").toInt(&okSeconds), cycles = parser.value("cycles").toInt(&okCycles);
    if (!okSeconds || !okCycles || seconds < 1 || seconds > 600 || cycles < 1 || cycles > 100
        || (parser.isSet("test-tone") && kind != Audio::InputKind::SystemLoopback)) return 2;
    std::unique_ptr<TestTone> tone;
    std::unique_ptr<QAudioSink> output;
    if (parser.isSet("test-tone")) {
        auto device = QMediaDevices::defaultAudioOutput();
        if (device.isNull()) return 3;
        tone = std::make_unique<TestTone>(device.preferredFormat());
        output = std::make_unique<QAudioSink>(device, device.preferredFormat());
        output->start(tone.get());
        if (output->error() != QtAudio::NoError) return 3;
    }
    quint64 chunks = 0, samples = 0, nonzero = 0, zero = 0, previousSequence = 0, gaps = 0;
    double peak = 0, squareSum = 0;
    qint64 startTime = 0, latency = -1;
    int cycle = 0, result = 0;
    bool stopped = true, invalidChunk = false;
    QTimer deadline, windows;
    deadline.setSingleShot(true); windows.setInterval(1000);
    double windowPeak = 0; quint64 windowSamples = 0;
    QObject::connect(&windows, &QTimer::timeout, [&] {
        print({{"event", "level"}, {"cycle", cycle}, {"samples", double(windowSamples)}, {"peak", windowPeak}});
        windowPeak = 0; windowSamples = 0;
    });
    QObject::connect(&coordinator, &AudioInputCoordinator::pcmReady, [&](const Audio::PcmChunk &chunk) {
        if (stopped || chunk.samples.size() != Audio::ChunkBytes
            || (chunks && chunk.sequence <= previousSequence)) invalidChunk = true;
        previousSequence = chunk.sequence; gaps += chunk.discontinuity;
        ++chunks;
        for (int i = 0; i < chunk.samples.size(); i += 2) {
            const auto s = qFromLittleEndian<qint16>(chunk.samples.constData() + i);
            const double v = double(s) / 32768;
            peak = std::max(peak, std::abs(v)); windowPeak = std::max(windowPeak, std::abs(v));
            squareSum += v * v; ++samples; ++windowSamples;
            if (s) ++nonzero; else ++zero;
        }
    });
    QObject::connect(&coordinator, &AudioInputCoordinator::errorOccurred, [&](const Audio::Error &e) {
        print({{"event", "error"}, {"code", int(e.code)}, {"message", e.message}, {"detail", e.detail}});
        result = 1; coordinator.stop(); app.quit();
    });
    std::function<void()> begin;
    begin = [&] {
        ++cycle; chunks = samples = zero = nonzero = previousSequence = gaps = 0;
        peak = squareSum = windowPeak = 0; windowSamples = 0; latency = -1; stopped = false;
        startTime = Audio::monotonicUs();
        print({{"event", "start"}, {"cycle", cycle}, {"kind", name}, {"device", parser.value("device")},
               {"unified", "16000 Hz / mono / int16 LE / 640 bytes"}});
        coordinator.start(kind, id); deadline.start(10000);
    };
    QObject::connect(&coordinator, &AudioInputCoordinator::stateChanged, [&](Audio::State state) {
        if (state == Audio::State::Running) {
            latency = (Audio::monotonicUs() - startTime) / 1000;
            const auto device = coordinator.selectedDevice();
            print({{"event", "running"}, {"cycle", cycle}, {"native", Audio::describe(coordinator.nativeFormat())},
                   {"description", device.description},
                   {"selected_id", kind == Audio::InputKind::Microphone ? QString::fromLatin1(device.id.toHex()) : QString::fromUtf8(device.id)},
                   {"start_ms", double(latency)}});
            deadline.start(seconds * 1000); windows.start();
        }
    });
    QObject::connect(&deadline, &QTimer::timeout, [&] {
        if (coordinator.state() != Audio::State::Running) {
            print({{"event", "startup_timeout"}}); result = 1; coordinator.stop(); app.quit(); return;
        }
        if (parser.isSet("exit-while-running")) {
            print({{"event", "exit_while_running"}, {"chunks", double(chunks)}});
            app.quit(); return;
        }
        windows.stop(); stopped = true;
        const auto before = chunks; const auto dropped = coordinator.droppedChunks(); auto stopTime = Audio::monotonicUs();
        coordinator.stop(); coordinator.stop();
        print({{"event", "summary"}, {"cycle", cycle}, {"chunks", double(chunks)}, {"bytes", double(samples * 2)},
            {"samples", double(samples)}, {"nonzero", double(nonzero)}, {"zero", double(zero)},
            {"peak", peak}, {"rms", samples ? std::sqrt(squareSum / samples) : 0}, {"discontinuities", double(gaps)},
            {"dropped_chunks", double(dropped)},
            {"start_ms", double(latency)}, {"stop_ms", double(Audio::monotonicUs() - stopTime) / 1000},
            {"stopped", coordinator.state() == Audio::State::Stopped}, {"invalid_chunk", invalidChunk}});
        QTimer::singleShot(200, &app, [&, before] {
            if (chunks != before || invalidChunk) { result = 1; app.quit(); }
            else if (cycle < cycles) begin();
            else app.quit();
        });
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &coordinator, &AudioInputCoordinator::stop);
    QTimer::singleShot(0, &app, begin);
    app.exec();
    if (output) output->stop();
    return result;
}
