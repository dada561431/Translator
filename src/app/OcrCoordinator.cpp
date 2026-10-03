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
    const quint64 request = ++latestRequest_;
    const QImage image = capture.image;
    auto *worker = static_cast<OcrWorker *>(worker_);
    QPointer<OcrCoordinator> coordinator(this);
    QMetaObject::invokeMethod(worker, [coordinator, worker, request, image, sourceLanguage] {
        const OcrResult result = worker->recognize(image, sourceLanguage);
#ifndef NDEBUG
        qDebug().noquote()
            << QStringLiteral("[OCR]\n"
                              "engine = %1\n"
                              "sourceLanguage = %2\n"
                              "tesseractLanguage = %3\n"
                              "tessdata = %4\n"
                              "input = %5x%6\n"
                              "processed = %7x%8\n"
                              "preprocess = %9 (%10 ms)\n"
                              "psm = %11\n"
                              "ocr = %12 ms\n"
                              "elapsed = %13 ms\n"
                              "text = \"%14\"\n"
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
                if (coordinator && request == coordinator->latestRequest_) {
                    emit coordinator->resultReady(result);
                }
            }, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
}
