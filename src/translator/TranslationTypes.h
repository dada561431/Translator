#pragma once

#include <QMetaType>
#include <QString>

struct TranslationRequest
{
    quint64 requestId = 0;
    QString sourceText;
    QString sourceLanguage;
    QString targetLanguage;
};

struct TranslationResult
{
    quint64 requestId = 0;
    QString sourceText;
    QString translatedText;
    QString sourceLanguage;
    QString targetLanguage;
    bool success = false;
    QString error;
    QString provider;
    int httpStatus = 0;
    qint64 elapsedMs = 0;
};

enum class TranslationState { Idle, Pending, Success, Error };

Q_DECLARE_METATYPE(TranslationRequest)
Q_DECLARE_METATYPE(TranslationResult)
Q_DECLARE_METATYPE(TranslationState)
