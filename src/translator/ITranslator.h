#pragma once

#include <QObject>
#include "translator/TranslationTypes.h"

class ITranslator : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual QString id() const = 0;
    virtual void translate(const TranslationRequest &request) = 0;

signals:
    void resultReady(const TranslationResult &result);
};
