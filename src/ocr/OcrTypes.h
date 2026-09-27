#pragma once

#include <QString>

struct OcrResult
{
    QString text;
    QString engineId;
    QString error;
    qint64 elapsedMs = 0;

    bool isValid() const { return error.isEmpty(); }
};
