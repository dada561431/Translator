#pragma once

#include "ocr/IOcrEngine.h"

#include <memory>

class TesseractOcrEngine final : public IOcrEngine
{
public:
    TesseractOcrEngine();
    ~TesseractOcrEngine() override;

    QString id() const override;
    OcrResult recognize(const QImage &image, const QString &sourceLanguage) override;

    TesseractOcrEngine(const TesseractOcrEngine &) = delete;
    TesseractOcrEngine &operator=(const TesseractOcrEngine &) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
