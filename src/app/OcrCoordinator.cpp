#include "app/OcrCoordinator.h"

#include <QDebug>
#include <QMetaObject>
#include <QPointer>

namespace {

class OcrWorker final : public QObject
{
public:
    explicit OcrWorker(OcrCoordinator::EngineFactory factory)
        : factory_(std::move(factory))
    {
    }

    OcrResult recognize(const QImage &image, const QString &sourceLanguage)
    {
        if (!initialized_) {
            initialized_ = true;
            if (factory_) {
                engine_ = factory_();
            }
        }
        if (!engine_) {
            OcrResult result;
            result.error = QStringLiteral("OCR engine is unavailable.");
            return result;
        }
        OcrResult result = engine_->recognize(image, sourceLanguage);
        result.sourceLanguage = sourceLanguage;
        if (result.engineId.isEmpty()) {
            result.engineId = engine_->id();
        }
        return result;
    }

private:
    OcrCoordinator::EngineFactory factory_;
    std::unique_ptr<IOcrEngine> engine_;
    bool initialized_ = false;
};

} // namespace

OcrCoordinator::OcrCoordinator(EngineFactory engineFactory, QObject *parent)
    : QObject(parent)
    , worker_(new OcrWorker(std::move(engineFactory)))
{
    worker_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    workerThread_.start();
}

OcrCoordinator::~OcrCoordinator()
{
    workerThread_.quit();
    workerThread_.wait();
}

void OcrCoordinator::recognize(const CaptureResult &capture,
                               const QString &sourceLanguage)
{
    if (busy_) {
        pending_ = true;
        pendingCapture_ = capture;
        pendingLanguage_ = sourceLanguage;
        return;
    }
    tryRecognize(capture, sourceLanguage);
}

void OcrCoordinator::discardPending()
{
    pending_ = false;
    pendingCapture_ = {};
    pendingLanguage_.clear();
}

quint64 OcrCoordinator::tryRecognize(const CaptureResult &capture,
                                    const QString &sourceLanguage)
{
    if (busy_) return 0;
    busy_ = true;
    const quint64 request = ++latestRequest_;
    const QImage image = capture.image;
    auto *worker = static_cast<OcrWorker *>(worker_);
    QPointer<OcrCoordinator> coordinator(this);
    QMetaObject::invokeMethod(worker, [coordinator, worker, request, image, sourceLanguage] {
        const OcrResult result = worker->recognize(image, sourceLanguage);
#ifndef NDEBUG
        qDebug().noquote()
            << QStringLiteral("[OCR] engine=%1 source=%2 tessLanguage=%3 tessdata=%4 "
                              "input=%5x%6 processed=%7x%8 preprocess=%9(%10ms) "
                              "psm=%11 ocrMs=%12 elapsedMs=%13 text=\"%14\" "
                              "error = \"%15\"")
                   .arg(result.engineId, sourceLanguage,
                        result.tesseractLanguage, result.tessdataPath)
                   .arg(result.inputSize.width()).arg(result.inputSize.height())
                   .arg(result.processedSize.width()).arg(result.processedSize.height())
                   .arg(result.preprocessingMode).arg(result.preprocessingMs)
                   .arg(result.pageSegmentationMode).arg(result.recognitionMs)
                   .arg(result.elapsedMs).arg(result.text, result.error);
#endif
        if (coordinator) {
            QMetaObject::invokeMethod(coordinator, [coordinator, request, result] {
                if (!coordinator) return;
                coordinator->busy_ = false;
                if (!coordinator->pending_) emit coordinator->resultReady(result);
                if (!coordinator) return;
                emit coordinator->taskFinished(request, result);
                if (!coordinator) return;
                if (coordinator->pending_ && !coordinator->busy_) {
                    const auto capture = coordinator->pendingCapture_;
                    const auto language = coordinator->pendingLanguage_;
                    coordinator->discardPending();
                    coordinator->tryRecognize(capture, language);
                }
            }, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
    return request;
}
