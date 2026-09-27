#pragma once

#include <QImage>
#include <QString>

#include "ocr/OcrTypes.h"

class IOcrEngine
{
public:
    virtual ~IOcrEngine() = default;

    virtual QString id() const = 0;
    // sourceLanguage uses the hint IDs auto, zh, en, ja, and ko.
    virtual OcrResult recognize(const QImage &image, const QString &sourceLanguage) = 0;
};
