#pragma once

#include <memory>
#include "ocr/OcrTypes.h"
#include "translator/ITranslator.h"

class SettingsManager;

class TranslationCoordinator final : public QObject
{
    Q_OBJECT
public:
    TranslationCoordinator(SettingsManager &settings, std::unique_ptr<ITranslator> translator,
                           QObject *parent = nullptr);
    void acceptOcr(const OcrResult &result);
    void invalidate();

signals:
    void stateChanged(TranslationState state);
    void resultReady(const TranslationResult &result);

private:
    void receiveResult(const TranslationResult &result);
    SettingsManager &settings_;
    std::unique_ptr<ITranslator> translator_;
    quint64 nextRequestId_ = 0;
    quint64 activeRequestId_ = 0;
};
