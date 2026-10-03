#pragma once

#include <QSize>
#include <QString>

struct OcrResult
{
    QString text;
    QString engineId;
    QString error;
    qint64 elapsedMs = 0;
    QString tesseractLanguage;
    QString tessdataPath;
    QSize inputSize;
    QSize processedSize;
    QString preprocessingMode;
    int pageSegmentationMode = 0;
    qint64 preprocessingMs = 0;
    qint64 recognitionMs = 0;
    QString sourceLanguage;

    bool isValid() const { return error.isEmpty(); }
};
