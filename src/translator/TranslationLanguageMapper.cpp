#include "translator/TranslationLanguageMapper.h"

bool TranslationLanguageMapper::isSupportedSource(const QString &id)
{
    return id == QLatin1String("auto") || !sourceCode(id).isEmpty();
}

QString TranslationLanguageMapper::sourceCode(const QString &id)
{
    if (id == QLatin1String("en")) return QStringLiteral("EN");
    if (id == QLatin1String("zh")) return QStringLiteral("ZH");
    if (id == QLatin1String("ja")) return QStringLiteral("JA");
    if (id == QLatin1String("ko")) return QStringLiteral("KO");
    return {};
}

QString TranslationLanguageMapper::targetCode(const QString &id)
{
    if (id == QLatin1String("en")) return QStringLiteral("EN-US");
    if (id == QLatin1String("zh")) return QStringLiteral("ZH-HANS");
    if (id == QLatin1String("ja")) return QStringLiteral("JA");
    if (id == QLatin1String("ko")) return QStringLiteral("KO");
    return {};
}
