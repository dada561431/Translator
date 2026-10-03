#pragma once

#include <QByteArray>
#include <QSettings>
#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>

class SettingsManager final : public QObject
{
    Q_OBJECT
public:
    SettingsManager();

    QString sourceLanguage();
    QString targetLanguage();
    QString ocrEngine();
    QString translator();
    QString deepLPlan() const;
    QString deepLEndpoint() const;
    QString openAiBaseUrl() const;
    QString openAiModel() const;
    QByteArray windowGeometry() const;
    QRect captureRegion() const;
    QString captureScreen() const;

    void setSourceLanguage(const QString &languageId);
    void setTargetLanguage(const QString &languageId);
    void setOcrEngine(const QString &engineId);
    void setTranslator(const QString &translatorId);
    void setDeepLPlan(const QString &plan);
    void setOpenAiBaseUrl(const QString &url);
    void setOpenAiModel(const QString &model);
    void notifyCredentialsChanged();
    void setWindowGeometry(const QByteArray &geometry);
    void setCaptureRegion(const QRect &region, const QString &screenName);

signals:
    void translationSettingsChanged();

private:
    QString readValidated(const QString &key,
                          const QStringList &allowedValues,
                          const QString &defaultValue);
    void writeValidated(const QString &key,
                        const QString &value,
                        const QStringList &allowedValues,
                        const QString &defaultValue);

    QSettings settings_;
    void writeTranslationValue(const QString &key, const QString &value);
};
