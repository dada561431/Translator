#include "asr/AsrCoordinator.h"
#include "asr/WhisperCppAsrBackend.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QtEndian>
#include <iostream>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#endif

namespace {
void print(QJsonObject data) {
    std::cout << QJsonDocument(data).toJson(QJsonDocument::Compact).constData() << std::endl;
}
qint64 memoryBytes() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) return qint64(counters.WorkingSetSize);
#endif
    return -1;
}
QByteArray readWav(const QString &path, QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return {}; }
    const auto header = file.read(12);
    if (header.size() != 12 || header.left(4) != "RIFF" || header.mid(8, 4) != "WAVE") {
        error = QStringLiteral("Expected RIFF WAVE."); return {};
    }
    const qint64 end = qint64(qFromLittleEndian<quint32>(header.constData() + 4)) + 8;
    if (end > file.size() || end < 12 || file.size() > 64000000) {
        error = QStringLiteral("Invalid or oversized WAV container."); return {};
    }
    bool format = false; QByteArray pcm;
    while (file.pos() + 8 <= end) {
        const auto chunk = file.read(8);
        const auto size = qFromLittleEndian<quint32>(chunk.constData() + 4);
        const auto next = file.pos() + size + (size & 1);
        if (next > end) { error = QStringLiteral("Truncated WAV chunk."); return {}; }
        if (chunk.left(4) == "fmt ") {
            if (size < 16) { error = QStringLiteral("Truncated WAV format."); return {}; }
            const auto fmt = file.read(16);
            format = qFromLittleEndian<quint16>(fmt.constData()) == 1
                && qFromLittleEndian<quint16>(fmt.constData() + 2) == 1
                && qFromLittleEndian<quint32>(fmt.constData() + 4) == 16000
                && qFromLittleEndian<quint32>(fmt.constData() + 8) == 32000
                && qFromLittleEndian<quint16>(fmt.constData() + 12) == 2
                && qFromLittleEndian<quint16>(fmt.constData() + 14) == 16;
        } else if (chunk.left(4) == "data") {
            if (size > AsrAudioBuffer::MaximumBytes || !pcm.isEmpty()) { error = QStringLiteral("WAV exceeds 30 seconds or contains multiple data chunks."); return {}; }
            pcm = file.read(size);
        }
        if (!file.seek(next)) { error = QStringLiteral("WAV seek failed."); return {}; }
    }
    if (!format || pcm.isEmpty() || pcm.size() % 2) {
        error = QStringLiteral("Probe accepts non-empty 16 kHz mono PCM16 LE WAV, at most 30 seconds; no resampling."); return {};
    }
    return pcm;
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser; parser.addHelpOption();
    parser.setApplicationDescription(QStringLiteral("Local ASR WAV QA only. No audio device, UI, translation, VAD or download."));
    parser.addOptions({{"model", "Prepared local ggml Whisper model", "path"},
        {"wav", "16 kHz mono PCM16 WAV (<=30s)", "path"},
        {"language", "auto|en|zh|ja|ko", "code", "auto"},
        {"partial-interval-ms", "Explicit partial snapshot interval (>=1000)", "n", "1000"},
        {"repeat", "Utterances reusing one loaded model (1-20)", "n", "1"},
        {"threads", "CPU thread count (1-64)", "n", "4"},
        {"cancel-after-ms", "Cancel first inference after n ms, then run recovery utterances", "n", "0"},
        {"exit-after-ms", "Exit during first inference to measure destructor/join", "n", "0"},
        {"silence", "Use explicit 2-second zero-PCM lifecycle fixture, not speech acceptance"}});
    parser.process(app);
    auto number = [&](const char *key, int low, int high) {
        bool ok = false; const int n = parser.value(QLatin1String(key)).toInt(&ok);
        return ok && n >= low && n <= high ? n : -1;
    };
    const int repeat = number("repeat", 1, 20), interval = number("partial-interval-ms", 1000, 30000);
    const int threads = number("threads", 1, 64), cancelMs = number("cancel-after-ms", 0, 60000), exitMs = number("exit-after-ms", 0, 60000);
    if (!parser.isSet("model") || repeat < 0 || interval < 0 || interval % 20 || threads < 0 || cancelMs < 0 || exitMs < 0
        || !Asr::supportedLanguage(parser.value("language")) || (cancelMs && exitMs)) return 2;
    QString error;
    QByteArray pcm = parser.isSet("silence") ? QByteArray(64000, '\0') : readWav(parser.value("wav"), error);
    if (pcm.isEmpty()) { print({{"event", "input_error"}, {"message", error}}); return 2; }
    const double durationMs = double(pcm.size()) / 32;
    const int padding = (640 - int(pcm.size()) % 640) % 640;
    pcm.append(QByteArray(padding, '\0'));
    int loads = 0, partials = 0, finals = 0, iteration = 0, offset = 0;
    bool cancelTriggered = false, exitTriggered = false, cancelled = false;
    quint64 cancelledSession = 0;
    QElapsedTimer total; total.start();
    int status = 0;
    {
        AsrCoordinator coordinator([] { return std::make_unique<WhisperCppAsrBackend>(); });
        QTimer feed, recovery;
        feed.setInterval(20); feed.setTimerType(Qt::PreciseTimer); recovery.setInterval(10);
        auto begin = [&] {
            ++iteration; offset = 0;
            if (!coordinator.beginUtterance({parser.value("language"), 60000, threads})) return;
            print({{"event", "begin"}, {"iteration", iteration}, {"session", qint64(coordinator.session())},
                {"audio_ms", durationMs}, {"tail_zero_padding_bytes", padding}});
            feed.start();
        };
        QObject::connect(&feed, &QTimer::timeout, &app, [&] {
            const quint64 seq = quint64(offset / 640);
            if (!coordinator.pushPcm({pcm.mid(offset, 640), quint64(iteration), seq, qint64(seq) * 20000, false})) { feed.stop(); return; }
            offset += 640;
            if (offset == pcm.size()) { feed.stop(); coordinator.finalizeUtterance(); }
            else if ((offset / 640 * 20) % interval == 0) coordinator.requestPartial();
        });
        QObject::connect(&coordinator, &AsrCoordinator::modelLoaded, &app, [&](qint64 ms) {
            ++loads;
            print({{"event", "model_loaded"}, {"load_ms", ms}, {"load_count", loads}, {"working_set_bytes", memoryBytes()}, {"backend", "CPU"}});
            coordinator.loadModel(parser.value("model")); // Same path must be a no-op.
            begin();
        });
        QObject::connect(&coordinator, &AsrCoordinator::stateChanged, &app, [&](Asr::State state) {
            if (state != Asr::State::Recognizing) return;
            if (cancelMs && !cancelTriggered) {
                cancelTriggered = true;
                QTimer::singleShot(cancelMs, &app, [&] {
                    feed.stop(); cancelledSession = coordinator.session(); cancelled = true;
                    coordinator.cancelUtterance();
                    print({{"event", "cancel"}, {"session", qint64(cancelledSession)}}); recovery.start();
                });
            }
            if (exitMs && !exitTriggered) {
                exitTriggered = true;
                QTimer::singleShot(exitMs, &app, [&] { feed.stop(); print({{"event", "exit_during_inference"}, {"busy", coordinator.isBusy()}}); app.quit(); });
            }
        });
        QObject::connect(&recovery, &QTimer::timeout, &app, [&] {
            if (!coordinator.isBusy()) { recovery.stop(); print({{"event", "cancel_drained"}}); begin(); }
        });
        QObject::connect(&coordinator, &AsrCoordinator::errorOccurred, &app, [&](const Asr::Error &e) {
            print({{"event", "error"}, {"code", int(e.code)}, {"message", e.message}}); status = 1; app.quit();
        });
        QObject::connect(&coordinator, &AsrCoordinator::resultReady, &app, [&](const Asr::Result &r) {
            if (cancelled && r.session == cancelledSession) { print({{"event", "stale_result_failure"}}); status = 1; app.quit(); return; }
            const bool final = r.kind == Asr::ResultKind::Final;
            if (final) ++finals; else ++partials;
            print({{"event", final ? "final" : "partial"}, {"text", r.text}, {"language", r.detectedLanguage},
                {"session", qint64(r.session)}, {"utterance", qint64(r.utterance)}, {"revision", qint64(r.revision)},
                {"processing_ms", r.processingMs}, {"snapshot_audio_ms", r.audioDurationMs},
                {"rtf_final", final ? r.processingMs / durationMs : 0}, {"working_set_bytes", memoryBytes()}});
            if (final) {
                if (finals < repeat) begin();
                else { coordinator.unloadModel(); recovery.stop(); QTimer::singleShot(0, &app, &QCoreApplication::quit); }
            }
        });
        QTimer::singleShot(0, &app, [&] { coordinator.loadModel(parser.value("model")); });
        QTimer::singleShot(120000 + repeat * 100000, &app, [&] { status = 1; print({{"event", "probe_watchdog"}}); app.quit(); });
        app.exec();
        feed.stop(); recovery.stop();
        print({{"event", "pre_teardown"}, {"total_ms", total.elapsed()}, {"model_load_count", loads},
            {"partial_count", partials}, {"final_count", finals}, {"cancelled", cancelled}, {"working_set_bytes", memoryBytes()}});
        total.restart();
    }
    print({{"event", "teardown"}, {"join_unload_ms", total.elapsed()}, {"working_set_bytes", memoryBytes()}});
    return status;
}
