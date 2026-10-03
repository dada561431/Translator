#pragma once

#include <QString>

class TranslationLanguageMapper final
{
public:
    // Empty source code means provider detection; invalid IDs are validated separately.
    static bool isSupportedSource(const QString &id);
    static QString sourceCode(const QString &id);
    static QString targetCode(const QString &id);
};
