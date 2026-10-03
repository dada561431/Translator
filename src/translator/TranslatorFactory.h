#pragma once

#include "translator/ITranslator.h"
#include "credentials/ICredentialStore.h"
#include <memory>

class SettingsManager;
class QNetworkAccessManager;

class TranslatorFactory
{
public:
    static QString resolveCredential(const QString &provider, const ICredentialStore &store,
                                     QString *error = nullptr);
    static std::unique_ptr<ITranslator> create(SettingsManager &settings, const ICredentialStore &store,
                                               QNetworkAccessManager *network = nullptr);
};
