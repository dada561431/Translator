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
    const quint64 request = ++latestRequest_;
    const QImage image = capture.image;
    auto *worker = static_cast<OcrWorker *>(worker_);
    QPointer<OcrCoordinator> coordinator(this);
    QMetaObject::invokeMethod(worker, [coordinator, worker, request, image, sourceLanguage] {
        const OcrResult result = worker->recognize(image, sourceLanguage);
#ifndef NDEBUG
        qDebug() << "[OCR] engine:" << result.engineId
                 << "input size:" << image.size()
                 << "sourceLanguage:" << sourceLanguage
                 << "elapsed:" << result.elapsedMs
                 << "text:" << result.text;
#endif
        if (coordinator) {
            QMetaObject::invokeMethod(coordinator, [coordinator, request, result] {
                if (coordinator && request == coordinator->latestRequest_) {
                    emit coordinator->resultReady(result);
                }
            }, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
}
