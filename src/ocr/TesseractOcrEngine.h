#pragma once

#include "ocr/IOcrEngine.h"
#include "ocr/OcrImagePreprocessor.h"

#include <memory>

class TesseractOcrEngine final : public IOcrEngine
{
public:
    explicit TesseractOcrEngine(const QString &tessdataPath = QString());
    ~TesseractOcrEngine() override;

    QString id() const override;
    OcrResult recognize(const QImage &image, const QString &sourceLanguage) override;
    OcrResult recognizeWithOptions(const QImage &image, const QString &sourceLanguage,
                                   const OcrPreprocessOptions &options,
                                   int pageSegmentationMode = 0);

    TesseractOcrEngine(const TesseractOcrEngine &) = delete;
    TesseractOcrEngine &operator=(const TesseractOcrEngine &) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
