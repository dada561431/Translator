#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace StreamingProbe {
struct TextChange {
    QString previous, text, stable, unstable;
    int commonPrefix = 0, added = 0, removed = 0;
    bool stableConflict = false;
};
class TextStabilizer {
public:
    TextChange update(const QString &text);
    void reset() { previous_.clear(); stable_.clear(); }
private:
    QString previous_, stable_;
};

struct QueueStats { size_t bytes = 0, highWaterBytes = 0; quint64 dropped = 0; };
class PcmMailbox {
public:
    explicit PcmMailbox(size_t capacity = 32000) : capacity_(capacity) {}
    bool push(QByteArray pcm, bool discontinuity = false);
    bool waitPop(QByteArray &pcm, bool *discontinuity = nullptr);
    void close();
    QueueStats stats() const;
private:
    const size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::pair<QByteArray, bool>> chunks_;
    QueueStats stats_;
    bool closed_ = false;
};

class IOnlineRecognizer {
public:
    virtual ~IOnlineRecognizer() = default;
    virtual QJsonObject load() = 0;
    virtual void accept(const std::vector<float> &samples) = 0;
    virtual bool ready() = 0;
    virtual void decode() = 0;
    virtual QString text() = 0;
    virtual bool endpoint() = 0;
    virtual void reset() = 0;
    virtual void finish() = 0;
};

// One worker owns every recognizer call and its destruction. No queued PCM events.
class Worker {
public:
    using Factory = std::function<std::unique_ptr<IOnlineRecognizer>()>;
    using Sink = std::function<void(QJsonObject)>;
    Worker(Factory factory, Sink sink, bool endpoints, size_t capacity = 32000, int flushMs = 300);
    ~Worker();
    bool start();
    void beginAudio();
    bool push(const QByteArray &pcm, bool discontinuity = false);
    void stop();
    bool failed() const;
    QueueStats queueStats() const { return mailbox_.stats(); }
private:
    void run();
    Factory factory_;
    Sink sink_;
    bool endpoints_;
    size_t capacity_;
    int flushMs_;
    PcmMailbox mailbox_;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable stateChanged_;
    bool loaded_ = false, failed_ = false, begun_ = false, stopping_ = false;
    qint64 originUs_ = 0;
    qint64 stopRequestedUs_ = 0;
};
qint64 workingSetBytes();
double processCpuMs();
}
