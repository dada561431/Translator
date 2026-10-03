#pragma once

#include "credentials/ICredentialStore.h"

class WindowsCredentialStore final : public ICredentialStore
{
public:
    explicit WindowsCredentialStore(const QString &nameSpace = QStringLiteral("Translator/Translation"))
        : namespace_(nameSpace) {}
    bool saveSecret(const QString &provider, const QString &secret, QString *error = nullptr) override;
    QString loadSecret(const QString &provider, QString *error = nullptr) const override;
    bool removeSecret(const QString &provider, QString *error = nullptr) override;
    static QString targetName(const QString &provider);
private:
    QString namespace_;
};
