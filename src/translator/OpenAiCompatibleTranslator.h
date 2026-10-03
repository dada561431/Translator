#pragma once

#include <QNetworkAccessManager>
#include <QUrl>
#include "translator/ITranslator.h"

struct OpenAiCompatibleConfiguration
{
    QString baseUrl;
    QString apiKey;
    QString model;
    QString error;
    int timeoutMs = 15000;
};

class OpenAiCompatibleTranslator final : public ITranslator
{
    Q_OBJECT
public:
    explicit OpenAiCompatibleTranslator(const OpenAiCompatibleConfiguration &configuration,
                                       QNetworkAccessManager *manager = nullptr, QObject *parent = nullptr);
    QString id() const override;
    void translate(const TranslationRequest &request) override;
    static QUrl completionUrl(const QString &baseUrl, QString *error = nullptr);
    static TranslationResult parseResponse(const TranslationRequest &request, const QByteArray &body,
                                           int status, const QString &model);
private:
    OpenAiCompatibleConfiguration configuration_;
    QNetworkAccessManager *manager_;
};
