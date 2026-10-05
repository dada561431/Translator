#include "ocr/OcrEngineFactory.h"
#include "ocr/TesseractOcrEngine.h"

std::unique_ptr<IOcrEngine> OcrEngineFactory::create(const QString &id, const PaddleHelperOptions &options)
{
    if (id == QLatin1String("tesseract")) return std::make_unique<TesseractOcrEngine>();
    if (id == QLatin1String("paddle-small")) return std::make_unique<PaddleOcrEngine>(options);
    return {};
}
