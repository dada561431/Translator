#pragma once

#include "credentials/ICredentialStore.h"
#include <QHash>

class MemoryCredentialStore final : public ICredentialStore
{
public:
    bool saveSecret(const QString &provider, const QString &secret, QString *error = nullptr) override
    {
        if (error) *error = failWrites ? QStringLiteral("Test storage failure.") : QString();
        if (failWrites) return false;
        secrets.insert(provider, secret);
        return true;
    }
    QString loadSecret(const QString &provider, QString *error = nullptr) const override
    {
        if (error) error->clear();
        return secrets.value(provider);
    }
    bool removeSecret(const QString &provider, QString *error = nullptr) override
    {
        if (error) *error = failWrites ? QStringLiteral("Test storage failure.") : QString();
        if (failWrites) return false;
        secrets.remove(provider);
        return true;
    }
    QHash<QString, QString> secrets;
    bool failWrites = false;
};
