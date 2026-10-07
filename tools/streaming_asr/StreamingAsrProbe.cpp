#include "SherpaOnlineRecognizer.h"
#include "audio/AudioInputCoordinator.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QPushButton>
#include <QStyle>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QTimer>
#include <QUrl>
#include <QtEndian>
#include <cmath>
#include <iostream>

namespace {
std::mutex outputMutex;
void print(QJsonObject event) {
    std::lock_guard<std::mutex> lock(outputMutex);
    std::cout << QJsonDocument(event).toJson(QJsonDocument::Compact).constData() << std::endl;
}
QByteArray readWav(const QString &path, QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return {}; }
    const auto header = file.read(12);
    if (header.size() != 12 || header.left(4) != "RIFF" || header.mid(8, 4) != "WAVE") {
        error = QStringLiteral("Expected RIFF WAVE"); return {};
    }
    const qint64 end = qint64(qFromLittleEndian<quint32>(header.constData() + 4)) + 8;
    if (end > file.size() || end < 12 || file.size() > 64000000) {
        error = QStringLiteral("Invalid WAV size"); return {};
    }
    bool format = false; QByteArray pcm;
    while (file.pos() + 8 <= end) {
        const auto chunk = file.read(8);
        const auto size = qFromLittleEndian<quint32>(chunk.constData() + 4);
        const qint64 next = file.pos() + size + (size & 1);
        if (next > end) { error = QStringLiteral("Truncated WAV chunk"); return {}; }
        if (chunk.left(4) == "fmt ") {
            if (size < 16) { error = QStringLiteral("Truncated format"); return {}; }
            const auto f = file.read(16);
            format = qFromLittleEndian<quint16>(f.constData()) == 1
                && qFromLittleEndian<quint16>(f.constData() + 2) == 1
                && qFromLittleEndian<quint32>(f.constData() + 4) == 16000
                && qFromLittleEndian<quint32>(f.constData() + 8) == 32000
                && qFromLittleEndian<quint16>(f.constData() + 12) == 2
                && qFromLittleEndian<quint16>(f.constData() + 14) == 16;
        } else if (chunk.left(4) == "data") {
            if (size > 16000 * 60 * 2 || !pcm.isEmpty()) { error = QStringLiteral("Maximum 60s, single data chunk"); return {}; }
            pcm = file.read(size);
        }
        if (!file.seek(next)) { error = QStringLiteral("WAV seek failed"); return {}; }
    }
    if (!format || pcm.isEmpty() || pcm.size() % 2) {
        error = QStringLiteral("Require 16 kHz mono PCM16 LE WAV; no resampling"); return {};
    }
    return pcm;
}
}
int main(int argc, char **argv) {
    bool wantPreview = false;
    for (int i = 1; i < argc; ++i) if (QByteArray(argv[i]) == "--preview") wantPreview = true;
    std::unique_ptr<QCoreApplication> application;
    if (wantPreview) application = std::make_unique<QApplication>(argc, argv);
    else application = std::make_unique<QCoreApplication>(argc, argv);
    auto &app = *application;
    QCommandLineParser p; p.addHelpOption();
    p.setApplicationDescription("Standalone local streaming ASR QA. No translation, production UI, recording or downloads.");
    p.addOptions({{"model-dir", "Prepared bilingual int8 Paraformer", "path"},
        {"wav", "Real-time paced 16k mono PCM16 WAV", "path"},
        {"kind", "microphone|loopback", "source"}, {"device", "Device ID as hex", "hex"},
        {"seconds", "Live capture duration (1-300)", "n", "45"},
        {"chunk-ms", "WAV feed interval: 20|40|100|200", "n", "20"},
        {"threads", "ORT CPU threads (1-16)", "n", "2"},
        {"flush-ms", "Explicit Stop-only zero tail A/B (0-2000); excluded from real audio", "n", "300"},
        {"endpoint", "Enable sherpa internal endpoint, reset without model reload"},
        {"speech-start-ms", "Manually verified WAV speech onset, -1 unknown", "n", "-1"},
        {"speech-end-ms", "Manually verified WAV speech end, -1 unknown", "n", "-1"},
        {"play-wav", "Local QA playback through output; repeat option for playlist (loopback only)", "path"},
        {"preview", "Separate QA-only text window; refreshed at most 10 Hz"},
        {"cue", "Repeatable live microphone reading cues, one every 15s", "text"},
        {"list-devices", "Enumerate only; do not load model or capture"}});
    p.process(app);
    AudioInputCoordinator audio;
    if (p.isSet("list-devices")) {
        for (auto kind : {Audio::InputKind::Microphone, Audio::InputKind::SystemLoopback})
            for (const auto &d : audio.devices(kind)) print({{"event", "device"},
                {"kind", kind == Audio::InputKind::Microphone ? "microphone" : "loopback"},
                {"id_hex", QString::fromLatin1(d.id.toHex())}, {"name", d.description}, {"default", d.isDefault}});
        return 0;
    }
    bool threadsOk, secondsOk, chunkOk, startOk, endOk, flushOk;
    const int threads = p.value("threads").toInt(&threadsOk), seconds = p.value("seconds").toInt(&secondsOk);
    const int chunk = p.value("chunk-ms").toInt(&chunkOk);
    const int flushMs = p.value("flush-ms").toInt(&flushOk);
    const int speechStart = p.value("speech-start-ms").toInt(&startOk), speechEnd = p.value("speech-end-ms").toInt(&endOk);
    const bool wav = p.isSet("wav"), loopback = p.value("kind") == "loopback";
    if (!p.isSet("model-dir") || wav == p.isSet("kind") || !threadsOk || threads < 1 || threads > 16
        || !secondsOk || seconds < 1 || seconds > 300 || !chunkOk || (chunk != 20 && chunk != 40 && chunk != 100 && chunk != 200)
        || !startOk || !endOk || speechStart < -1 || speechEnd < -1
        || !flushOk || flushMs < 0 || flushMs > 2000
        || (!wav && !loopback && p.value("kind") != "microphone") || (p.isSet("play-wav") && !loopback)) return 2;
    QString error; QByteArray pcm;
    if (wav) {
        pcm = readWav(p.value("wav"), error);
        if (pcm.isEmpty() || speechStart > pcm.size() / 32 || speechEnd > pcm.size() / 32
            || (speechStart >= 0 && speechEnd >= 0 && speechEnd <= speechStart)) {
            print({{"event", "input_error"}, {"message", error.isEmpty() ? "Invalid speech span" : error}}); return 2;
        }
    }
    const QByteArray hex = p.value("device").toLatin1();
    const QByteArray id = QByteArray::fromHex(hex);
    if (!hex.isEmpty() && id.toHex() != hex.toLower()) return 2;
    const auto playlist = p.values("play-wav");
    for (const auto &path : playlist) {
        if (readWav(path, error).isEmpty()) { print({{"event", "input_error"}, {"message", error}}); return 2; }
    }
    const bool endpoint = p.isSet("endpoint");
    std::mutex previewMutex; QString latestText;
    std::unique_ptr<QDialog> preview;
    QLabel *cueLabel = nullptr; QPlainTextEdit *textView = nullptr;
    if (wantPreview) {
        preview = std::make_unique<QDialog>(); preview->setWindowTitle("Streaming ASR QA - no translation");
        auto *layout = new QVBoxLayout(preview.get());
        cueLabel = new QLabel("Loading local model...", preview.get()); cueLabel->setWordWrap(true);
        layout->addWidget(cueLabel);
        textView = new QPlainTextEdit(preview.get()); textView->setReadOnly(true); textView->setMaximumBlockCount(50);
        auto font = textView->font(); font.setPointSize(18); textView->setFont(font); layout->addWidget(textView);
        auto *stopButton = new QPushButton("Stop", preview.get()); stopButton->setIcon(preview->style()->standardIcon(QStyle::SP_MediaStop));
        layout->addWidget(stopButton); QObject::connect(stopButton, &QPushButton::clicked, &app, &QCoreApplication::quit);
        QObject::connect(preview.get(), &QDialog::finished, &app, &QCoreApplication::quit);
        preview->resize(760, 280); preview->show(); app.processEvents();
    }
    int partialBeforeEnd = 0; qint64 firstPartial = -1;
    StreamingProbe::Worker worker([&] { return StreamingProbe::createSherpa(p.value("model-dir"), threads, endpoint); },
        [&](QJsonObject e) {
            if (e.value("event") == "partial" || e.value("event") == "final") {
                std::lock_guard<std::mutex> lock(previewMutex);
                latestText = e.value("text").toString();
            }
            if (e.value("event") == "partial" && !e.value("text").toString().isEmpty()) {
                const qint64 at = qint64(e.value("wall_ms").toDouble());
                if (firstPartial < 0) firstPartial = at;
                if (speechEnd >= 0 && at < speechEnd) ++partialBeforeEnd;
            }
            print(e);
        }, endpoint, 32000, flushMs);
    if (!worker.start()) { worker.stop(); return 1; }
    QTimer feed, stop, health;
    feed.setTimerType(Qt::PreciseTimer); feed.setInterval(2);
    stop.setSingleShot(true); health.setInterval(250);
    QElapsedTimer clock; int offset = 0, status = 0; double maxLate = 0;
    QTimer previewTimer; previewTimer.setInterval(100);
    QString shown; int lastCue = -2;
    const auto cues = p.values("cue");
    QObject::connect(&previewTimer, &QTimer::timeout, &app, [&] {
        { std::lock_guard<std::mutex> lock(previewMutex);
          if (textView && latestText != shown) { shown = latestText; textView->setPlainText(shown); } }
        if (!clock.isValid()) return;
        const int cue = clock.elapsed() < 3000 ? -1 : int((clock.elapsed() - 3000) / 15000);
        if (cue != lastCue && cue < cues.size()) {
            lastCue = cue;
            if (cueLabel) cueLabel->setText(cue < 0 ? QStringLiteral("Get ready; read each cue once, then remain quiet.") : cues[cue]);
            if (cue >= 0) print({{"event", "cue"}, {"index", cue}, {"wall_ms", clock.elapsed()}, {"text", cues[cue]}, {"not_actual_speech_onset", true}});
        }
    });
    previewTimer.start();
    QMediaPlayer player; QAudioOutput output;
    player.setAudioOutput(&output); output.setVolume(1.0);
    int playbackIndex = 0;
    auto playNext = [&] {
        if (playbackIndex >= playlist.size()) return;
        print({{"event", "playback_start"}, {"index", playbackIndex}, {"wall_ms", clock.elapsed()}, {"path", playlist[playbackIndex]}});
        player.setSource(QUrl::fromLocalFile(QFileInfo(playlist[playbackIndex++]).absoluteFilePath())); player.play();
    };
    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &app, [&](QMediaPlayer::MediaStatus s) {
        if (s == QMediaPlayer::EndOfMedia) {
            print({{"event", "playback_end"}, {"index", playbackIndex - 1}, {"wall_ms", clock.elapsed()}});
            QTimer::singleShot(3000, &app, playNext);
        }
    });
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &app, [&](QMediaPlayer::Error, const QString &message) {
        print({{"event", "playback_error"}, {"message", message}}); status = 1; app.quit();
    });
    QObject::connect(&stop, &QTimer::timeout, &app, &QCoreApplication::quit);
    QObject::connect(&health, &QTimer::timeout, &app, [&] {
        const auto q = worker.queueStats();
        print({{"event", "queue"}, {"wall_ms", clock.elapsed()}, {"backlog_ms", q.bytes / 32.}, {"dropped_chunks", qint64(q.dropped)}});
        if (worker.failed() || q.dropped) { status = 1; app.quit(); }
    });
    QObject::connect(&feed, &QTimer::timeout, &app, [&] {
        // Each batch represents audio that has already elapsed. Never fast-feed a whole file.
        const int amount = std::min(chunk * 32, int(pcm.size()) - offset);
        const double due = (offset + amount) / 32.;
        if (clock.elapsed() < due) return;
        maxLate = std::max(maxLate, clock.elapsed() - due);
        if (!worker.push(pcm.mid(offset, amount))) { status = 1; app.quit(); return; }
        offset += amount;
        if (offset >= pcm.size()) { feed.stop(); app.quit(); }
    });
    double levelEnergy = 0, levelPeak = 0; int levelSamples = 0, liveSamples = 0;
    QObject::connect(&audio, &AudioInputCoordinator::pcmReady, &app, [&](const Audio::PcmChunk &c) {
        if (c.samples.size() != Audio::ChunkBytes || !worker.push(c.samples, c.discontinuity)) {
            print({{"event", "rejected_pcm"}}); status = 1; app.quit(); return;
        }
        if (c.discontinuity) {
            print({{"event", "capture_discontinuity"}, {"sequence", qint64(c.sequence)},
                {"wall_ms", clock.elapsed()}, {"audio_ms", liveSamples / 16.}});
        }
        // Diagnostic levels only; do not gate input or endpoint the recognizer.
        for (int i = 0; i < c.samples.size(); i += 2) {
            const double v = qFromLittleEndian<qint16>(c.samples.constData() + i) / 32768.;
            levelEnergy += v * v; levelPeak = std::max(levelPeak, std::abs(v)); ++levelSamples;
        }
        liveSamples += Audio::ChunkSamples;
        if (levelSamples >= 8000) {
            print({{"event", "level"}, {"wall_ms", clock.elapsed()}, {"audio_ms", liveSamples / 16.},
                {"rms", std::sqrt(levelEnergy / levelSamples)}, {"peak", levelPeak}});
            levelEnergy = levelPeak = 0; levelSamples = 0;
        }
    });
    QObject::connect(&audio, &AudioInputCoordinator::errorOccurred, &app, [&](const Audio::Error &e) {
        print({{"event", "capture_error"}, {"message", e.message}}); status = 1; app.quit();
    });
    QObject::connect(&audio, &AudioInputCoordinator::stateChanged, &app, [&](Audio::State s) {
        if (s == Audio::State::Running) {
            const auto d = audio.selectedDevice();
            print({{"event", "capture_running"}, {"kind", loopback ? "loopback" : "microphone"},
                {"device", d.description}, {"device_id_hex", QString::fromLatin1(d.id.toHex())},
                {"native_format", Audio::describe(audio.nativeFormat())}});
            worker.beginAudio(); clock.start(); stop.start(seconds * 1000); health.start();
            if (!playlist.isEmpty()) QTimer::singleShot(1000, &app, playNext);
        }
    });
    print({{"event", "input"}, {"kind", wav ? "wav" : loopback ? "loopback" : "microphone"},
        {"chunk_ms", wav ? chunk : 20}, {"speech_start_ms", speechStart}, {"speech_end_ms", speechEnd},
        {"speech_span_provenance", speechStart >= 0 ? "external_manual_annotation" : "unknown"},
        {"translation_requests", 0}});
    QTimer::singleShot(0, &app, [&] {
        if (wav) { worker.beginAudio(); clock.start(); feed.start(); health.start(); }
        else if (!audio.start(loopback ? Audio::InputKind::SystemLoopback : Audio::InputKind::Microphone, id)) {
            status = 1; app.quit();
        }
    });
    QTimer::singleShot(10000, &app, [&] {
        if (!clock.isValid()) { print({{"event", "capture_start_watchdog"}}); status = 1; app.quit(); }
    });
    app.exec();
    feed.stop(); stop.stop(); health.stop(); previewTimer.stop(); player.stop();
    const quint64 captureDrops = audio.droppedChunks(); audio.stop();
    QElapsedTimer teardown; teardown.start(); worker.stop();
    if (worker.failed() || worker.queueStats().dropped || captureDrops) status = 1;
    print({{"event", "teardown"}, {"join_unload_ms", teardown.elapsed()}, {"max_feed_lateness_ms", maxLate},
        {"speech_start_to_first_partial_ms", speechStart >= 0 && firstPartial >= 0 ? firstPartial - speechStart : -1},
        {"nonempty_partials_before_speech_end", speechEnd >= 0 ? partialBeforeEnd : -1},
        {"capture_dropped_chunks", qint64(captureDrops)}, {"worker_joined", true}, {"translation_requests", 0}, {"status", status}});
    return status;
}
