#pragma once
#include "ocr/PaddleOcrEngine.h"
#include <memory>

class OcrEngineFactory
{
public:
    static std::unique_ptr<IOcrEngine> create(const QString &id,
        const PaddleHelperOptions &options = PaddleHelperOptions::fromEnvironment());
};
