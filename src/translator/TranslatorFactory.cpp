#include "translator/TranslatorFactory.h"
#include "translator/DeepLTranslator.h"
#include "translator/OpenAiCompatibleTranslator.h"
#include "config/SettingsManager.h"

QString TranslatorFactory::resolveCredential(const QString &provider, const ICredentialStore &store, QString *error)
{
    QString storeError;
    QString secret = store.loadSecret(provider, &storeError);
    if (error) *error = storeError;
    if (!storeError.isEmpty()) return {};
    if (secret.isEmpty() && provider == QLatin1String("deepl"))
        secret = qEnvironmentVariable("DEEPL_API_KEY").trimmed();
    return secret;
}

std::unique_ptr<ITranslator> TranslatorFactory::create(SettingsManager &settings, const ICredentialStore &store,
                                                     QNetworkAccessManager *network)
{
    const QString provider = settings.translator();
    if (provider == QLatin1String("none")) return nullptr;
    QString error;
    const QString key = resolveCredential(provider, store, &error);
    if (provider == QLatin1String("deepl")) {
        DeepLConfiguration configuration;
        configuration.apiKey = key;
        configuration.endpoint = QUrl(settings.deepLEndpoint());
        configuration.error = error;
        return std::make_unique<DeepLTranslator>(configuration, network);
    }
    if (provider == QLatin1String("openai_compatible")) {
        OpenAiCompatibleConfiguration configuration;
        configuration.apiKey = key;
        configuration.baseUrl = settings.openAiBaseUrl();
        configuration.model = settings.openAiModel();
        configuration.error = error;
        return std::make_unique<OpenAiCompatibleTranslator>(configuration, network);
    }
    return nullptr;
}
