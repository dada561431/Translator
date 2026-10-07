#pragma once
#include "audio/AudioInputCoordinator.h"
#include "asr/AsrCoordinator.h"
#include "app/TranslationCoordinator.h"
#include <QElapsedTimer>
#include <optional>

class AudioTranslationCoordinator final : public QObject
{
    Q_OBJECT
public:
    struct Configuration {
        Audio::InputKind kind = Audio::InputKind::Microphone;
        QByteArray deviceId;
        Asr::Options asr;
        QString translationSource = QStringLiteral("auto");
        QString translationTarget = QStringLiteral("zh");
        int segmentMs = 4000;
    };
    AudioTranslationCoordinator(AudioInputCoordinator &audio, AsrCoordinator &asr,
                                TranslationCoordinator &translation, QObject *parent = nullptr);
    ~AudioTranslationCoordinator() override;
    bool start(const Configuration &configuration);
    void stop();
    void finalizeBoundary();
    bool isRunning() const { return running_; }
    quint64 session() const { return session_; }
    int bufferedChunks() const { return int(current_.size() + (pending_ ? pending_->chunks.size() : 0)); }
    quint64 droppedSegments() const { return dropped_; }
signals:
    void runningChanged(bool running);
    void originalTextReady(const QString &text);
    void translatedTextReady(const QString &text);
    void feedback(const QString &message);
    void segmentFinalized(quint64 session, quint64 utterance, qint64 durationMs);
    void finalReady(quint64 session, quint64 utterance, const Asr::Result &result);
    void translationRequested(quint64 session, quint64 utterance, const TranslationRequest &request);
    void translationFinished(quint64 session, quint64 utterance, const TranslationResult &result,
                             qint64 postBoundaryMs);
private:
    struct Segment { quint64 id; QList<Audio::PcmChunk> chunks; qint64 boundaryUs; };
    void receivePcm(const Audio::PcmChunk &chunk);
    void dispatch();
    void receiveAsr(const Asr::Result &result);
    void receiveError(const Asr::Error &error);
    AudioInputCoordinator &audio_;
    AsrCoordinator &asr_;
    TranslationCoordinator &translation_;
    Configuration configuration_;
    QTimer boundary_, dispatchTimer_;
    QList<Audio::PcmChunk> current_;
    std::optional<Segment> pending_, active_;
    quint64 session_ = 0, nextUtterance_ = 0, audioSession_ = 0;
    quint64 asrSession_ = 0, asrUtterance_ = 0, displayed_ = 0;
    quint64 requestId_ = 0, requestUtterance_ = 0, requestSession_ = 0, dropped_ = 0;
    qint64 requestBoundaryUs_ = 0;
    bool running_ = false, submitting_ = false, prepared_ = false;
};
