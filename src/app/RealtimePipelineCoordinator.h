#pragma once
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
#include <optional>
#include "capture/ScreenCaptureService.h"
#include "processing/FrameComparator.h"
#include "processing/TextDeduplicator.h"
#include "ocr/OcrTypes.h"

class SettingsManager;
class OcrCoordinator;
class TranslationCoordinator;

struct RealtimeOptions
{
    int intervalMs = 300;
    int emptyResultsToClear = 2;
    int captureFailuresToStop = 4;
};

struct RealtimeStatistics
{
    quint64 captures = 0, unchangedFrames = 0, changedFrames = 0;
    quint64 ocrStarted = 0, pendingReplaced = 0, staleOcr = 0;
    quint64 duplicateTexts = 0, translationsStarted = 0, captureFailures = 0;
    qint64 maxCaptureUs = 0, maxDiffUs = 0;
};

class RealtimePipelineCoordinator final : public QObject
{
    Q_OBJECT
public:
    using CaptureFunction = std::function<CaptureResult(const QRect &, const QString &)>;
    RealtimePipelineCoordinator(SettingsManager &settings, OcrCoordinator &ocr,
                                TranslationCoordinator &translation, QObject *parent = nullptr,
                                CaptureFunction capture = {}, RealtimeOptions options = {});
    bool start();
    void stop();
    void tick();
    void acceptOneShot(const CaptureResult &capture);
    bool isRunning() const { return running_; }
    quint64 sessionId() const { return session_; }
    int pendingCount() const { return pending_ ? 1 : 0; }
    const RealtimeStatistics &statistics() const { return statistics_; }

signals:
    void runningChanged(bool running);
    void feedback(const QString &message);
    void originalTextReady(const QString &text);
    void subtitlesCleared();
    void sampleAccepted(quint64 session, quint64 frame, const OcrResult &result,
                        qint64 captureToOcrMs);

private:
    enum class Mode { Stopped, OneShot, Realtime };
    struct Frame {
        CaptureResult capture;
        QString language;
        quint64 session = 0, sequence = 0;
        qint64 detectedMs = 0;
    };
    void resetContentTracking();
    void enqueue(const CaptureResult &capture);
    void dispatchPending();
    void completed(quint64 request, const OcrResult &result);
    void settingsChanged();
    void logStatistics();

    SettingsManager &settings_;
    OcrCoordinator &ocr_;
    TranslationCoordinator &translation_;
    CaptureFunction capture_;
    RealtimeOptions options_;
    QTimer timer_, statisticsTimer_;
    QElapsedTimer clock_;
    FrameComparator comparator_;
    TextDeduplicator deduplicator_;
    RealtimeStatistics statistics_;
    std::optional<Frame> pending_;
    Frame active_;
    quint64 activeRequest_ = 0, session_ = 0, sequence_ = 0;
    int consecutiveEmpty_ = 0, consecutiveCaptureFailures_ = 0;
    bool running_ = false, capturing_ = false, cleared_ = false;
    bool retryOcr_ = false;
    Mode mode_ = Mode::Stopped;
};
