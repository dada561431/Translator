#include "credentials/WindowsCredentialStore.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif

namespace {
void setError(QString *error, const QString &message)
{
    if (error) *error = message;
}
bool supportedProvider(const QString &provider)
{
    return provider == QLatin1String("deepl") || provider == QLatin1String("openai_compatible");
}
}

QString WindowsCredentialStore::targetName(const QString &provider)
{
    return QStringLiteral("Translator/Translation/") + provider;
}

bool WindowsCredentialStore::saveSecret(const QString &provider, const QString &secret, QString *error)
{
    setError(error, {});
    if (!supportedProvider(provider) || secret.trimmed().isEmpty()) {
        setError(error, QStringLiteral("Invalid credential provider or empty API key."));
        return false;
    }
#ifdef Q_OS_WIN
    QByteArray blob = secret.toUtf8();
    if (blob.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        setError(error, QStringLiteral("API key exceeds Windows credential size limit."));
        return false;
    }
    std::wstring target = (namespace_ + QLatin1Char('/') + provider).toStdWString();
    std::wstring account = L"API Key";
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.UserName = account.data();
    credential.CredentialBlobSize = DWORD(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    const bool saved = CredWriteW(&credential, 0);
    const DWORD code = saved ? 0 : GetLastError();
    SecureZeroMemory(blob.data(), SIZE_T(blob.size()));
    if (!saved) setError(error, QStringLiteral("Windows credential write failed (code %1).").arg(code));
    return saved;
#else
    setError(error, QStringLiteral("Secure credential storage is unsupported on this platform."));
    return false;
#endif
}

QString WindowsCredentialStore::loadSecret(const QString &provider, QString *error) const
{
    setError(error, {});
    if (!supportedProvider(provider)) {
        setError(error, QStringLiteral("Invalid credential provider."));
        return {};
    }
#ifdef Q_OS_WIN
    const std::wstring target = (namespace_ + QLatin1Char('/') + provider).toStdWString();
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        const DWORD code = GetLastError();
        if (code != ERROR_NOT_FOUND)
            setError(error, QStringLiteral("Windows credential read failed (code %1).").arg(code));
        return {};
    }
    const QString secret = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),
                                             int(credential->CredentialBlobSize));
    if (credential->CredentialBlob)
        SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    return secret;
#else
    setError(error, QStringLiteral("Secure credential storage is unsupported on this platform."));
    return {};
#endif
}

bool WindowsCredentialStore::removeSecret(const QString &provider, QString *error)
{
    setError(error, {});
    if (!supportedProvider(provider)) {
        setError(error, QStringLiteral("Invalid credential provider."));
        return false;
    }
#ifdef Q_OS_WIN
    const std::wstring target = (namespace_ + QLatin1Char('/') + provider).toStdWString();
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) return true;
    const DWORD code = GetLastError();
    if (code == ERROR_NOT_FOUND) return true;
    setError(error, QStringLiteral("Windows credential delete failed (code %1).").arg(code));
    return false;
#else
    setError(error, QStringLiteral("Secure credential storage is unsupported on this platform."));
    return false;
#endif
}

std::unique_ptr<ICredentialStore> createPlatformCredentialStore()
{
    // The non-Windows implementation fails safely; it never falls back to plaintext.
    return std::make_unique<WindowsCredentialStore>();
}
