#include "app/TranslationCoordinator.h"
#include "config/SettingsManager.h"
#include "translator/TranslationLanguageMapper.h"

#include <QDateTime>
#include <QDebug>

TranslationCoordinator::TranslationCoordinator(SettingsManager &settings,
                                             std::unique_ptr<ITranslator> translator,
                                             QObject *parent)
    : QObject(parent), settings_(settings), translator_(std::move(translator))
{
    if (translator_) {
        connect(translator_.get(), &ITranslator::resultReady,
                this, &TranslationCoordinator::receiveResult);
    }
    connect(&settings_, &SettingsManager::translationSettingsChanged,
            this, &TranslationCoordinator::invalidate);
}

void TranslationCoordinator::invalidate()
{
    activeRequestId_ = 0;
    emit stateChanged(TranslationState::Idle);
}

void TranslationCoordinator::acceptOcr(const OcrResult &ocr)
{
    invalidate();
    if (!ocr.isValid() || ocr.text.trimmed().isEmpty()
        || settings_.translator() == QLatin1String("none")) return;
    if (!ocr.sourceLanguage.isEmpty() && ocr.sourceLanguage != settings_.sourceLanguage()) return;

    TranslationRequest request;
    request.requestId = ++nextRequestId_;
    request.sourceText = ocr.text.trimmed();
    request.sourceLanguage = settings_.sourceLanguage();
    request.targetLanguage = settings_.targetLanguage();
    activeRequestId_ = request.requestId;
    emit stateChanged(TranslationState::Pending);
#ifndef NDEBUG
    qDebug() << "[Translation] request start" << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
             << "requestId=" << request.requestId << "sourceChars=" << request.sourceText.size()
             << "OCR elapsed=" << ocr.elapsedMs;
#endif

    if (TranslationLanguageMapper::isSupportedSource(request.sourceLanguage)
        && !TranslationLanguageMapper::targetCode(request.targetLanguage).isEmpty()
        && request.sourceLanguage == request.targetLanguage) {
        TranslationResult result;
        result.requestId = request.requestId;
        result.sourceText = request.sourceText;
        result.translatedText = request.sourceText;
        result.sourceLanguage = request.sourceLanguage;
        result.targetLanguage = request.targetLanguage;
        result.provider = QStringLiteral("local");
        result.success = true;
        receiveResult(result);
    } else if (translator_) {
        translator_->translate(request);
    } else {
        TranslationResult result;
        result.requestId = request.requestId;
        result.error = QStringLiteral("Translation backend is unavailable.");
        receiveResult(result);
    }
}

void TranslationCoordinator::receiveResult(const TranslationResult &result)
{
    if (!activeRequestId_ || result.requestId != activeRequestId_) {
#ifndef NDEBUG
        qDebug() << "[Translation] ignored stale result requestId=" << result.requestId;
#endif
        return;
    }
    activeRequestId_ = 0;
    TranslationResult validated = result;
    if (validated.success && validated.translatedText.trimmed().isEmpty()) {
        validated.success = false;
        validated.error = QStringLiteral("Translation backend returned empty text.");
    }
#ifndef NDEBUG
    qDebug() << "[Translation] requestId=" << validated.requestId << "provider=" << validated.provider
             << "source=" << validated.sourceLanguage << "target=" << validated.targetLanguage
             << "sourceChars=" << validated.sourceText.size() << "elapsed=" << validated.elapsedMs
             << "httpStatus=" << validated.httpStatus << "success=" << validated.success
             << "error=" << validated.error;
#endif
    if (!validated.success) qWarning() << "Translation failed:" << validated.error;
    emit stateChanged(validated.success ? TranslationState::Success : TranslationState::Error);
    emit resultReady(validated);
}
