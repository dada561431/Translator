#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>
#include <QStringList>

class SettingsManager final
{
public:
    SettingsManager();

    QString sourceLanguage();
    QString targetLanguage();
    QString ocrEngine();
    QString translator();
    QByteArray windowGeometry() const;

    void setSourceLanguage(const QString &languageId);
    void setTargetLanguage(const QString &languageId);
    void setOcrEngine(const QString &engineId);
    void setTranslator(const QString &translatorId);
    void setWindowGeometry(const QByteArray &geometry);

private:
    QString readValidated(const QString &key,
                          const QStringList &allowedValues,
                          const QString &defaultValue);
    void writeValidated(const QString &key,
                        const QString &value,
                        const QStringList &allowedValues,
                        const QString &defaultValue);

    QSettings settings_;
};
