#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include "translator/DeepLTranslator.h"
#include "config/SettingsManager.h"
#include "credentials/ICredentialStore.h"

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    DeepLConfiguration configuration = DeepLConfiguration::fromEnvironment();
    if (application.arguments().contains(QStringLiteral("--stored-credentials"))) {
        application.setOrganizationName(QStringLiteral("TranslatorProject"));
        application.setApplicationName(QStringLiteral("Translator"));
        auto store = createPlatformCredentialStore();
        QString error;
        configuration.apiKey = store->loadSecret(QStringLiteral("deepl"), &error);
        if (!error.isEmpty()) { output << "Secure credential read failed.\n"; return 1; }
        SettingsManager settings;
        configuration.endpoint = QUrl(settings.deepLEndpoint());
    }
    if (configuration.apiKey.trimmed().isEmpty()) {
        output << (application.arguments().contains(QStringLiteral("--stored-credentials"))
            ? "SKIPPED - no GUI DeepL credential configured\n"
            : "SKIPPED - DEEPL_API_KEY not configured\n");
        return 0;
    }
    DeepLTranslator translator(configuration);
    int failures = 0;
    QObject::connect(&translator, &ITranslator::resultReady, &application,
        [&](const TranslationResult &result) {
            output << result.sourceLanguage << " -> " << result.targetLanguage
                   << " success=" << result.success << " HTTP=" << result.httpStatus
                   << " elapsed=" << result.elapsedMs << "ms text=" << result.translatedText
                   << " error=" << result.error << '\n';
            if (!result.success || result.translatedText.isEmpty()) ++failures;
            if (result.requestId == 1) {
                translator.translate({2, QStringLiteral("你好世界"), QStringLiteral("zh"), QStringLiteral("en")});
            } else application.exit(failures ? 1 : 0);
        });
    QTimer::singleShot(0, &application, [&] {
        translator.translate({1, QStringLiteral("Hello world"), QStringLiteral("en"), QStringLiteral("zh")});
    });
    QTimer::singleShot(35000, &application, [&] { application.exit(2); });
    return application.exec();
}
