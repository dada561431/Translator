#pragma once
#include "asr/IAsrBackend.h"
#include "asr/AsrAudioBuffer.h"
#include <QObject>
#include <QThread>
#include <QTimer>
#include <functional>
#include <optional>

class AsrCoordinator final : public QObject
{
    Q_OBJECT
public:
    using Factory = std::function<std::unique_ptr<IAsrBackend>()>;
    explicit AsrCoordinator(Factory factory, QObject *parent = nullptr);
    ~AsrCoordinator() override;
    void loadModel(const QString &path);
    void unloadModel();
    bool beginUtterance(Asr::Options options = {});
    bool pushPcm(const Audio::PcmChunk &chunk);
    bool requestPartial();
    bool finalizeUtterance();
    void cancelUtterance();
    void stop(); // Cancels work and asynchronously unloads; destructor joins.
    Asr::State state() const { return state_; }
    bool isBusy() const { return busy_; }
    quint64 session() const { return session_; }
    quint64 utterance() const { return utterance_; }
    int bufferedBytes() const { return buffer_.size(); }
    int pendingJobs() const { return pending_ ? 1 : 0; }
signals:
    void stateChanged(Asr::State state);
    void modelLoaded(qint64 loadMs);
    void resultReady(const Asr::Result &result);
    void errorOccurred(const Asr::Error &error);
private:
    struct Job {
        quint64 session, utterance, revision;
        Asr::ResultKind kind;
        QByteArray pcm;
        Asr::Options options;
        bool discontinuity;
    };
    void assertOwner() const;
    void invalidate();
    void setState(Asr::State state);
    bool fail(Asr::ErrorCode code, const QString &message);
    bool queue(Asr::ResultKind kind);
    void dispatch();
    QThread thread_;
    QObject *worker_ = nullptr;
    QTimer deadline_;
    Asr::CancelToken token_;
    Asr::State state_ = Asr::State::Unloaded;
    AsrAudioBuffer buffer_;
    Asr::Options options_;
    std::optional<Job> pending_;
    QString desiredPath_, loadedPath_;
    quint64 session_ = 0, utterance_ = 0, revision_ = 0, operation_ = 0;
    bool busy_ = false, modelOperation_ = false, ready_ = false;
    bool accepting_ = false, finalizing_ = false;
};
