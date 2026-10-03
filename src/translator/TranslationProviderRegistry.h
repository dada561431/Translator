#pragma once

#include <QList>
#include <QString>

struct TranslationProviderInfo
{
    QString id;
    QString displayName;
    bool requiresApiKey;
    bool supportsCustomEndpoint;
    bool supportsModel;
    bool supportsAutoSourceLanguage;
    QString defaultEndpoint;
    bool supportsPlan;
};

class TranslationProviderRegistry
{
public:
    static const QList<TranslationProviderInfo> &providers();
    static const TranslationProviderInfo *find(const QString &id);
};
