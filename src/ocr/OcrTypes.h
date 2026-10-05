#pragma once

#include <QSize>
#include <QString>
#include <QStringList>
#include <QPolygonF>
#include <QList>

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
    int boxCount = 0;
    QList<double> confidences;
    QList<QPolygonF> boxes;
    qint64 helperPid = 0;
    QString helperRequestId;
    QStringList boxTexts;
    QString detectionScoreStatus;

    bool isValid() const { return error.isEmpty(); }
};
