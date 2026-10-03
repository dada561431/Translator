#pragma once

#include <QString>
#include <memory>

class ICredentialStore
{
public:
    virtual ~ICredentialStore() = default;
    virtual bool saveSecret(const QString &provider, const QString &secret, QString *error = nullptr) = 0;
    virtual QString loadSecret(const QString &provider, QString *error = nullptr) const = 0;
    virtual bool removeSecret(const QString &provider, QString *error = nullptr) = 0;
};

std::unique_ptr<ICredentialStore> createPlatformCredentialStore();
