#pragma once
#include <QString>

class TextDeduplicator final
{
public:
    static QString normalize(QString text);
    bool accept(const QString &normalized);
    void reset() { last_.clear(); }
private:
    QString last_;
};
