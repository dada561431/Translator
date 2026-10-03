#include <QCoreApplication>
#include <QTextStream>
#include <QUuid>
#include "credentials/WindowsCredentialStore.h"

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    if (application.arguments().contains(QStringLiteral("--status"))) {
        WindowsCredentialStore store;
        for (const auto &provider : {QStringLiteral("deepl"), QStringLiteral("openai_compatible")}) {
            QString error;
            const bool stored = !store.loadSecret(provider, &error).isEmpty();
            output << provider << " stored=" << stored << " readError=" << !error.isEmpty() << '\n';
        }
        return 0;
    }
    if (!application.arguments().contains(QStringLiteral("--self-test"))) {
        output << "Manual-only: use --self-test for isolated Windows credential lifecycle verification.\n";
        return 2;
    }
    const QString nameSpace = QStringLiteral("Translator/Translation/SelfTest/")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    WindowsCredentialStore store(nameSpace);
    const QString provider = QStringLiteral("deepl");
    QString error;
    const QString first = QUuid::createUuid().toString();
    const QString second = QUuid::createUuid().toString();
    const bool saved = store.saveSecret(provider, first, &error);
    const bool loaded = saved && store.loadSecret(provider, &error) == first && error.isEmpty();
    const bool replaced = loaded && store.saveSecret(provider, second, &error)
        && store.loadSecret(provider, &error) == second && error.isEmpty();
    const bool removed = store.removeSecret(provider, &error);
    const bool absent = removed && store.loadSecret(provider, &error).isEmpty() && error.isEmpty();
    output << "isolated credential: saved=" << saved << " loaded=" << loaded
           << " replaced=" << replaced << " removed=" << removed << " absent=" << absent << '\n';
    return saved && loaded && replaced && removed && absent ? 0 : 1;
}
