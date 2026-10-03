#pragma once

#include <QNetworkAccessManager>
#include <QUrl>
#include "translator/ITranslator.h"

struct DeepLConfiguration
{
    QString apiKey;
    QUrl endpoint = QUrl(QStringLiteral("https://api-free.deepl.com/v2/translate"));
    int timeoutMs = 15000;
    QString error;

    static DeepLConfiguration fromEnvironment();
};

class DeepLTranslator final : public ITranslator
{
    Q_OBJECT
public:
    explicit DeepLTranslator(QObject *parent = nullptr);
    // An injected manager allows offline transport tests; it must outlive this object.
    DeepLTranslator(const DeepLConfiguration &configuration,
                    QNetworkAccessManager *manager = nullptr, QObject *parent = nullptr);
    QString id() const override;
    void translate(const TranslationRequest &request) override;

    static TranslationResult parseResponse(const TranslationRequest &request,
                                           const QByteArray &body, int httpStatus);

private:
    void postResult(const TranslationResult &result);
    DeepLConfiguration configuration_;
    QNetworkAccessManager *manager_;
};
