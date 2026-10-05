#include "app/RealtimePipelineCoordinator.h"
#include "app/OcrCoordinator.h"
#include "app/TranslationCoordinator.h"
#include "config/SettingsManager.h"
#include <QGuiApplication>
#include <QScreen>
#include <QScopedValueRollback>
#include <QDebug>
#include <QDateTime>

RealtimePipelineCoordinator::RealtimePipelineCoordinator(
    SettingsManager &settings, OcrCoordinator &ocr, TranslationCoordinator &translation,
    QObject *parent, CaptureFunction capture, RealtimeOptions options)
    : QObject(parent), settings_(settings), ocr_(ocr), translation_(translation),
      capture_(std::move(capture)), options_(options)
{
    if (!capture_) capture_ = [](const QRect &region, const QString &name) {
        QScreen *selected = nullptr;
        for (auto *screen : QGuiApplication::screens())
            if (screen->name() == name) { selected = screen; break; }
        return ScreenCaptureService().capture(selected, region);
    };
    timer_.setInterval(qMax(1, options_.intervalMs));
    timer_.setTimerType(Qt::PreciseTimer);
    statisticsTimer_.setInterval(10000);
    connect(&timer_, &QTimer::timeout, this, &RealtimePipelineCoordinator::tick);
    connect(&statisticsTimer_, &QTimer::timeout, this, &RealtimePipelineCoordinator::logStatistics);
    connect(&ocr_, &OcrCoordinator::taskFinished, this, &RealtimePipelineCoordinator::completed);
    connect(&settings_, &SettingsManager::translationSettingsChanged,
            this, &RealtimePipelineCoordinator::settingsChanged);
    clock_.start();
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) {
        if (running_ && !settings_.captureRegion().isValid()) {
            stop();
            emit feedback(QStringLiteral("Screen missing. Please select region again."));
        }
    });
}

void RealtimePipelineCoordinator::resetContentTracking()
{
    comparator_.reset();
    deduplicator_.reset();
    pending_.reset();
    ocr_.discardPending();
    consecutiveEmpty_ = 0;
    consecutiveCaptureFailures_ = 0;
    retryOcr_ = false;
    cleared_ = false;
}

bool RealtimePipelineCoordinator::start()
{
    if (running_) return true;
    if (!settings_.captureRegion().isValid()) {
        emit feedback(QStringLiteral("Please select a capture region first."));
        return false;
    }
    ++session_;
    mode_ = Mode::Realtime;
    running_ = true;
    resetContentTracking();
    translation_.invalidate();
    emit runningChanged(true);
    emit feedback(QStringLiteral("Running"));
    timer_.start();
    statisticsTimer_.start();
    tick();
    return running_;
}

void RealtimePipelineCoordinator::stop()
{
    timer_.stop();
    statisticsTimer_.stop();
    if (mode_ == Mode::Stopped) return;
    ++session_;
    mode_ = Mode::Stopped;
    running_ = false;
    pending_.reset();
    ocr_.discardPending();
    translation_.invalidate(false);
    emit runningChanged(false);
    emit feedback(QStringLiteral("Stopped"));
    logStatistics();
}

void RealtimePipelineCoordinator::settingsChanged()
{
    ++session_;
    resetContentTracking();
    translation_.invalidate();
    if (!running_) mode_ = Mode::Stopped;
    // The next tick uses a fresh snapshot; the physical old OCR remains bounded.
}

void RealtimePipelineCoordinator::tick()
{
    if (!running_ || capturing_) return;
    const QScopedValueRollback<bool> guard(capturing_, true);
    const QRect region = settings_.captureRegion();
    if (!region.isValid()) {
        stop();
        emit feedback(QStringLiteral("Region lost. Please select region again."));
        return;
    }
    QElapsedTimer elapsed;
    elapsed.start();
    const CaptureResult capture = capture_(region, settings_.captureScreen());
    ++statistics_.captures;
    statistics_.maxCaptureUs = qMax(statistics_.maxCaptureUs, elapsed.nsecsElapsed() / 1000);
    if (!capture.isValid()) {
        ++statistics_.captureFailures;
        if (!consecutiveCaptureFailures_) qWarning() << "[Realtime] capture failed:" << capture.error;
        if (++consecutiveCaptureFailures_ >= options_.captureFailuresToStop) {
            stop();
            emit feedback(QStringLiteral("Capture repeatedly failed. Please select region again."));
        }
        return;
    }
    consecutiveCaptureFailures_ = 0;
    elapsed.restart();
    const bool changed = comparator_.accept(capture.image);
    statistics_.maxDiffUs = qMax(statistics_.maxDiffUs, elapsed.nsecsElapsed() / 1000);
    if (changed) ++statistics_.changedFrames;
    else ++statistics_.unchangedFrames;
    // A stable empty frame must be recognized twice, not held forever by frame dedup.
    if (changed || retryOcr_ || (consecutiveEmpty_ > 0 && !cleared_)) enqueue(capture);
}

void RealtimePipelineCoordinator::acceptOneShot(const CaptureResult &capture)
{
    if (!capture.isValid()) return;
    stop();
    ++session_;
    mode_ = Mode::OneShot;
    resetContentTracking();
    translation_.invalidate();
    enqueue(capture);
}

void RealtimePipelineCoordinator::enqueue(const CaptureResult &capture)
{
    if (pending_) ++statistics_.pendingReplaced;
    pending_ = Frame{capture, settings_.sourceLanguage(), settings_.ocrEngine(), session_, ++sequence_, clock_.elapsed()};
    dispatchPending();
}

void RealtimePipelineCoordinator::dispatchPending()
{
    if (!pending_ || ocr_.isBusy() || mode_ == Mode::Stopped) return;
    active_ = std::move(*pending_);
    pending_.reset();
    activeRequest_ = ocr_.tryRecognize(active_.capture, active_.language, active_.engine);
    if (activeRequest_) ++statistics_.ocrStarted;
}

void RealtimePipelineCoordinator::completed(quint64 request, const OcrResult &result)
{
    if (request != activeRequest_) { dispatchPending(); return; }
    activeRequest_ = 0;
    const Frame completedFrame = std::move(active_);
    active_ = {};
    if (completedFrame.session != session_ || mode_ == Mode::Stopped
        || completedFrame.language != settings_.sourceLanguage()
        || completedFrame.engine != settings_.ocrEngine()) {
        ++statistics_.staleOcr;
#ifndef NDEBUG
        qDebug() << "[Realtime] ignored stale OCR session=" << completedFrame.session
                 << "activeSession=" << session_ << "request=" << request;
#endif
        dispatchPending();
        return;
    }
    if (!result.isValid()) {
        consecutiveEmpty_ = 0;
        retryOcr_ = true;
        emit feedback(QStringLiteral("OCR failed: %1; previous subtitles kept.").arg(result.error.left(240)));
    } else {
        retryOcr_ = false;
        OcrResult normalized = result;
        normalized.text = TextDeduplicator::normalize(result.text);
        if (normalized.text.isEmpty()) {
            const int clearThreshold = mode_ == Mode::OneShot ? 1 : options_.emptyResultsToClear;
            if (++consecutiveEmpty_ >= clearThreshold && !cleared_) {
                cleared_ = true;
                deduplicator_.reset();
                translation_.invalidate();
                emit subtitlesCleared();
            }
        } else {
            consecutiveEmpty_ = 0;
            cleared_ = false;
            if (deduplicator_.accept(normalized.text)) {
                emit originalTextReady(normalized.text);
                if (completedFrame.session != session_ || mode_ == Mode::Stopped) {
                    dispatchPending();
                    return;
                }
                if (settings_.translator() != QLatin1String("none")) ++statistics_.translationsStarted;
                translation_.acceptOcr(normalized);
                emit sampleAccepted(completedFrame.session, completedFrame.sequence, normalized,
                                    clock_.elapsed() - completedFrame.detectedMs);
#ifndef NDEBUG
                qDebug() << "[Realtime] accepted session=" << session_ << "frame=" << completedFrame.sequence
                         << "captureToOcrMs=" << clock_.elapsed() - completedFrame.detectedMs
                         << "uiUtc=" << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
#endif
            } else ++statistics_.duplicateTexts;
        }
    }
    dispatchPending();
}

void RealtimePipelineCoordinator::logStatistics()
{
#ifndef NDEBUG
    qDebug() << "[Realtime] captures=" << statistics_.captures << "unchanged=" << statistics_.unchangedFrames
             << "changed=" << statistics_.changedFrames << "ocrStarted=" << statistics_.ocrStarted
             << "pendingReplaced=" << statistics_.pendingReplaced << "duplicates=" << statistics_.duplicateTexts
             << "translations=" << statistics_.translationsStarted << "stale=" << statistics_.staleOcr
             << "captureFailures=" << statistics_.captureFailures
             << "active=" << (ocr_.isBusy() ? 1 : 0) << "pending=" << pendingCount()
             << "maxCaptureUs=" << statistics_.maxCaptureUs << "maxDiffUs=" << statistics_.maxDiffUs;
#endif
}
