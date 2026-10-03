#include "translator/TranslationProviderRegistry.h"

const QList<TranslationProviderInfo> &TranslationProviderRegistry::providers()
{
    static const QList<TranslationProviderInfo> table = {
        {QStringLiteral("none"), QStringLiteral("None"), false, false, false, false, {}, false},
        {QStringLiteral("deepl"), QStringLiteral("DeepL"), true, false, false, true,
         QStringLiteral("https://api-free.deepl.com/v2/translate"), true},
        {QStringLiteral("openai_compatible"), QStringLiteral("OpenAI Compatible"),
         true, true, true, true, {}, false}
    };
    return table;
}

const TranslationProviderInfo *TranslationProviderRegistry::find(const QString &id)
{
    for (const auto &info : providers()) if (info.id == id) return &info;
    return nullptr;
}
