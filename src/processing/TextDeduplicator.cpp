#include "processing/TextDeduplicator.h"
#include <QRegularExpression>

QString TextDeduplicator::normalize(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar('\r'), QChar('\n'));
    static const QRegularExpression spaces(QStringLiteral("[^\\S\\n]+"));
    text.replace(spaces, QStringLiteral(" "));
    QStringList lines = text.split(QChar('\n'));
    for (QString &line : lines) line = line.trimmed();
    return lines.join(QChar('\n')).trimmed();
}

bool TextDeduplicator::accept(const QString &normalized)
{
    if (normalized.isEmpty() || normalized == last_) return false;
    last_ = normalized;
    return true;
}
