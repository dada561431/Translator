#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include "translator/DeepLTranslator.h"

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    if (qEnvironmentVariable("DEEPL_API_KEY").trimmed().isEmpty()) {
        output << "SKIPPED - DEEPL_API_KEY not configured\n";
        return 0;
    }
    DeepLTranslator translator;
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
