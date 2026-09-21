#include "config/SettingsManager.h"

#include <QGuiApplication>
#include <QScreen>

namespace {

const QString kSourceLanguageKey = QStringLiteral("language/source");
const QString kTargetLanguageKey = QStringLiteral("language/target");
const QString kOcrEngineKey = QStringLiteral("ocr/engine");
const QString kTranslatorKey = QStringLiteral("translator/engine");
const QString kWindowGeometryKey = QStringLiteral("window/geometry");
const QString kCaptureRegionKey = QStringLiteral("capture/region");
const QString kCaptureScreenKey = QStringLiteral("capture/screen");

const QString kDefaultSourceLanguage = QStringLiteral("auto");
const QString kDefaultTargetLanguage = QStringLiteral("zh");
const QString kDefaultOcrEngine = QStringLiteral("windows_ocr");
const QString kDefaultTranslator = QStringLiteral("none");

const QStringList kSourceLanguages = {
    QStringLiteral("auto"),
    QStringLiteral("zh"),
    QStringLiteral("en"),
    QStringLiteral("ja"),
    QStringLiteral("ko"),
};

const QStringList kTargetLanguages = {
    QStringLiteral("zh"),
    QStringLiteral("en"),
    QStringLiteral("ja"),
    QStringLiteral("ko"),
};

const QStringList kOcrEngines = {QStringLiteral("windows_ocr")};
const QStringList kTranslators = {QStringLiteral("none")};

} // namespace

SettingsManager::SettingsManager()
{
    sourceLanguage();
    targetLanguage();
    ocrEngine();
    translator();
}

QString SettingsManager::sourceLanguage()
{
    return readValidated(kSourceLanguageKey, kSourceLanguages, kDefaultSourceLanguage);
}

QString SettingsManager::targetLanguage()
{
    return readValidated(kTargetLanguageKey, kTargetLanguages, kDefaultTargetLanguage);
}

QString SettingsManager::ocrEngine()
{
    return readValidated(kOcrEngineKey, kOcrEngines, kDefaultOcrEngine);
}

QString SettingsManager::translator()
{
    return readValidated(kTranslatorKey, kTranslators, kDefaultTranslator);
}

QByteArray SettingsManager::windowGeometry() const
{
    return settings_.value(kWindowGeometryKey).toByteArray();
}

QRect SettingsManager::captureRegion() const
{
    const QRect region = settings_.value(kCaptureRegionKey).toRect();
    const QString screenName = captureScreen();
    if (region.width() < 10 || region.height() < 10) {
        return {};
    }

    for (const QScreen *screen : QGuiApplication::screens()) {
        if (screen->name() == screenName && screen->geometry().contains(region)) {
            return region;
        }
    }
    return {};
}

QString SettingsManager::captureScreen() const
{
    return settings_.value(kCaptureScreenKey).toString();
}

void SettingsManager::setSourceLanguage(const QString &languageId)
{
    writeValidated(kSourceLanguageKey, languageId, kSourceLanguages, kDefaultSourceLanguage);
}

void SettingsManager::setTargetLanguage(const QString &languageId)
{
    writeValidated(kTargetLanguageKey, languageId, kTargetLanguages, kDefaultTargetLanguage);
}

void SettingsManager::setOcrEngine(const QString &engineId)
{
    writeValidated(kOcrEngineKey, engineId, kOcrEngines, kDefaultOcrEngine);
}

void SettingsManager::setTranslator(const QString &translatorId)
{
    writeValidated(kTranslatorKey, translatorId, kTranslators, kDefaultTranslator);
}

void SettingsManager::setWindowGeometry(const QByteArray &geometry)
{
    settings_.setValue(kWindowGeometryKey, geometry);
    settings_.sync();
}

void SettingsManager::setCaptureRegion(const QRect &region, const QString &screenName)
{
    settings_.setValue(kCaptureRegionKey, region);
    settings_.setValue(kCaptureScreenKey, screenName);
    settings_.sync();
}

QString SettingsManager::readValidated(const QString &key,
                                       const QStringList &allowedValues,
                                       const QString &defaultValue)
{
    const QString value = settings_.value(key, defaultValue).toString();
    if (settings_.contains(key) && allowedValues.contains(value)) {
        return value;
    }

    settings_.setValue(key, defaultValue);
    settings_.sync();
    return defaultValue;
}

void SettingsManager::writeValidated(const QString &key,
                                     const QString &value,
                                     const QStringList &allowedValues,
                                     const QString &defaultValue)
{
    settings_.setValue(key, allowedValues.contains(value) ? value : defaultValue);
    settings_.sync();
}
