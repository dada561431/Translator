#pragma once

#include <QObject>
#include <QThread>

#include "capture/ScreenCaptureService.h"
#include "ocr/IOcrEngine.h"

#include <functional>
#include <memory>

class OcrCoordinator final : public QObject
{
    Q_OBJECT

public:
    using EngineFactory = std::function<std::unique_ptr<IOcrEngine>()>;

    explicit OcrCoordinator(EngineFactory engineFactory, QObject *parent = nullptr);
    ~OcrCoordinator() override;

    void recognize(const CaptureResult &capture, const QString &sourceLanguage);
    quint64 tryRecognize(const CaptureResult &capture, const QString &sourceLanguage);
    bool isBusy() const { return busy_; }
    void discardPending();

signals:
    void resultReady(const OcrResult &result);
    void taskFinished(quint64 requestId, const OcrResult &result);

private:
    QThread workerThread_;
    QObject *worker_ = nullptr;
    quint64 latestRequest_ = 0;
    bool busy_ = false;
    bool pending_ = false;
    CaptureResult pendingCapture_;
    QString pendingLanguage_;
};
