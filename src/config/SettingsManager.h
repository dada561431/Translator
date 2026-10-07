#pragma once

#include <QByteArray>
#include <QSettings>
#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include "config/OverlayAppearance.h"
#include "config/GlobalHotkeyConfig.h"
#include "config/AudioSettings.h"

class SettingsManager final : public QObject
{
    Q_OBJECT
public:
    SettingsManager();
    QString inputMode() const;
    void setInputMode(const QString &mode);
    AudioSettings audioSettings() const;
    void setAudioSettings(AudioSettings configuration);

    QString sourceLanguage();
    QString targetLanguage();
    QString ocrEngine();
    QString translator();
    QString deepLPlan() const;
    QString deepLEndpoint() const;
    QString openAiBaseUrl() const;
    QString openAiModel() const;
    QByteArray windowGeometry() const;
    bool overlayClickThrough() const;
    bool overlayDragLocked() const;
    GlobalHotkeyConfig globalHotkeys() const;
    void setGlobalHotkeys(const GlobalHotkeyConfig &config);
    void setOverlayDragLocked(bool locked);
    bool overlayExcludeFromCapture() const;
    OverlayAppearance overlayAppearance() const;
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
    void setOverlayClickThrough(bool enabled);
    void setOverlayExcludeFromCapture(bool enabled);
    void setOverlayAppearance(OverlayAppearance appearance);
    void setCaptureRegion(const QRect &region, const QString &screenName);

signals:
    void inputModeChanged();
    void audioSettingsChanged();
    void translationSettingsChanged();
    void overlayAppearanceChanged();
    void overlayDragLockedChanged();
    void overlayCaptureExclusionChanged();

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
