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
    using SelectedEngineFactory = std::function<std::unique_ptr<IOcrEngine>(const QString &)>;

    explicit OcrCoordinator(EngineFactory engineFactory, QObject *parent = nullptr,
                            SelectedEngineFactory selectedFactory = {});
    ~OcrCoordinator() override;

    void recognize(const CaptureResult &capture, const QString &sourceLanguage,
                   const QString &engineId = QStringLiteral("tesseract"));
    quint64 tryRecognize(const CaptureResult &capture, const QString &sourceLanguage,
                         const QString &engineId = QStringLiteral("tesseract"));
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
    QString pendingEngine_;
};
