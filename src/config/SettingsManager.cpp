#include "config/SettingsManager.h"
#include "translator/TranslationProviderRegistry.h"

#include <QGuiApplication>
#include <QScreen>
#include <QFont>
#include <cmath>

bool SettingsManager::overlayDragLocked() const
{
    return settings_.value(QStringLiteral("overlay/dragLocked"), false).toBool();
}
void SettingsManager::setOverlayDragLocked(bool locked)
{
    const bool changed = overlayDragLocked() != locked;
    settings_.setValue(QStringLiteral("overlay/dragLocked"), locked);
    settings_.sync();
    if (changed) emit overlayDragLockedChanged();
}
GlobalHotkeyConfig SettingsManager::globalHotkeys() const
{
    GlobalHotkeyConfig config;
    const QStringList keys{QStringLiteral("hotkeys/toggleInteraction"), QStringLiteral("hotkeys/region"), QStringLiteral("hotkeys/startStop")};
    for (int i = 0; i < 3; ++i) config.shortcuts[i] = settings_.value(keys[i], config.shortcuts[i]).toString();
    return config; // Do not overwrite malformed preferences on read.
}
void SettingsManager::setGlobalHotkeys(const GlobalHotkeyConfig &config)
{
    GlobalHotkeyConfig canonical;
    std::array<GlobalHotkeyChord, 3> chords;
    QString error;
    if (!parseGlobalHotkeys(config, canonical, chords, error)) return;
    const QStringList keys{QStringLiteral("hotkeys/toggleInteraction"), QStringLiteral("hotkeys/region"), QStringLiteral("hotkeys/startStop")};
    for (int i = 0; i < 3; ++i) settings_.setValue(keys[i], canonical.shortcuts[i]);
    settings_.sync();
}

namespace {

const QString kSourceLanguageKey = QStringLiteral("language/source");
const QString kTargetLanguageKey = QStringLiteral("language/target");
const QString kOcrEngineKey = QStringLiteral("ocr/engine");
const QString kTranslatorKey = QStringLiteral("translator/provider");
const QString kWindowGeometryKey = QStringLiteral("window/geometry");
const QString kCaptureRegionKey = QStringLiteral("capture/region");
const QString kCaptureScreenKey = QStringLiteral("capture/screen");

qreal safeFont(qreal value, qreal fallback)
{
    return std::isfinite(value) ? qBound(10.0, value, 72.0) : fallback;
}

OverlayAppearance normalized(OverlayAppearance appearance)
{
    appearance.translationFontSize = safeFont(appearance.translationFontSize, 16);
    appearance.originalFontSize = safeFont(appearance.originalFontSize, 12);
    appearance.backgroundOpacity = qBound(0, appearance.backgroundOpacity, 100);
    if (!appearance.showTranslation && !appearance.showOriginal) appearance.showTranslation = true;
    return appearance;
}

const QString kDefaultSourceLanguage = QStringLiteral("auto");
const QString kDefaultTargetLanguage = QStringLiteral("zh");
const QString kDefaultOcrEngine = QStringLiteral("tesseract");
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

const QStringList kOcrEngines = {QStringLiteral("tesseract"), QStringLiteral("paddle-small")};
const QStringList kTranslators = [] {
    QStringList ids;
    for (const auto &provider : TranslationProviderRegistry::providers()) ids.append(provider.id);
    return ids;
}();

} // namespace

SettingsManager::SettingsManager()
{
    const QString legacyKey = QStringLiteral("translator/engine");
    if (!settings_.contains(kTranslatorKey) && settings_.contains(legacyKey))
        settings_.setValue(kTranslatorKey, settings_.value(legacyKey));
    settings_.remove(legacyKey);
    settings_.sync();
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

QString SettingsManager::deepLPlan() const
{
    const QString plan = settings_.value(QStringLiteral("translator/deepl/plan")).toString();
    return plan == QLatin1String("pro") ? plan : QStringLiteral("free");
}

QString SettingsManager::deepLEndpoint() const
{
    if (settings_.contains(QStringLiteral("translator/deepl/plan")))
        return deepLPlan() == QLatin1String("pro")
            ? QStringLiteral("https://api.deepl.com/v2/translate")
            : QStringLiteral("https://api-free.deepl.com/v2/translate");
    const QString environment = qEnvironmentVariable("DEEPL_API_URL").trimmed();
    return environment.isEmpty() ? QStringLiteral("https://api-free.deepl.com/v2/translate") : environment;
}

QString SettingsManager::openAiBaseUrl() const
{
    return settings_.value(QStringLiteral("translator/openaiCompatible/baseUrl")).toString();
}

QString SettingsManager::openAiModel() const
{
    return settings_.value(QStringLiteral("translator/openaiCompatible/model")).toString();
}

void SettingsManager::writeTranslationValue(const QString &key, const QString &value)
{
    if (settings_.contains(key) && settings_.value(key).toString() == value) return;
    settings_.setValue(key, value);
    settings_.sync();
    emit translationSettingsChanged();
}

void SettingsManager::setDeepLPlan(const QString &plan)
{
    writeTranslationValue(QStringLiteral("translator/deepl/plan"),
                          plan == QLatin1String("pro") ? plan : QStringLiteral("free"));
}

void SettingsManager::setOpenAiBaseUrl(const QString &url)
{
    writeTranslationValue(QStringLiteral("translator/openaiCompatible/baseUrl"), url.trimmed());
}

void SettingsManager::setOpenAiModel(const QString &model)
{
    writeTranslationValue(QStringLiteral("translator/openaiCompatible/model"), model.trimmed());
}

void SettingsManager::notifyCredentialsChanged() { emit translationSettingsChanged(); }

QByteArray SettingsManager::windowGeometry() const
{
    return settings_.value(kWindowGeometryKey).toByteArray();
}

bool SettingsManager::overlayClickThrough() const
{
    return settings_.value(QStringLiteral("overlay/clickThrough"), false).toBool();
}

bool SettingsManager::overlayExcludeFromCapture() const
{
#ifdef Q_OS_WIN
    constexpr bool defaultValue = true;
#else
    constexpr bool defaultValue = false;
#endif
    return settings_.value(QStringLiteral("overlay/excludeFromCapture"), defaultValue).toBool();
}

OverlayAppearance SettingsManager::overlayAppearance() const
{
    OverlayAppearance appearance;
    const qreal base = QGuiApplication::font().pointSizeF();
    auto readFont = [this](const QString &key, qreal fallback) {
        bool ok = false;
        const qreal value = settings_.value(key, fallback).toDouble(&ok);
        return ok ? safeFont(value, fallback) : fallback;
    };
    appearance.translationFontSize = readFont(QStringLiteral("overlay/translationFontSize"), qMax(16.0, base + 6.0));
    appearance.originalFontSize = readFont(QStringLiteral("overlay/originalFontSize"), qMax(12.0, base + 2.0));
    appearance.backgroundOpacity = settings_.value(QStringLiteral("overlay/backgroundOpacity"), 0).toInt();
    appearance.showTranslation = settings_.value(QStringLiteral("overlay/showTranslation"), true).toBool();
    appearance.showOriginal = settings_.value(QStringLiteral("overlay/showOriginal"), true).toBool();
    return normalized(appearance);
}

void SettingsManager::setOverlayExcludeFromCapture(bool enabled)
{
    const bool changed = overlayExcludeFromCapture() != enabled;
    settings_.setValue(QStringLiteral("overlay/excludeFromCapture"), enabled);
    settings_.sync();
    if (changed) emit overlayCaptureExclusionChanged();
}

void SettingsManager::setOverlayAppearance(OverlayAppearance appearance)
{
    appearance = normalized(appearance);
    const bool changed = !(overlayAppearance() == appearance);
    settings_.setValue(QStringLiteral("overlay/translationFontSize"), appearance.translationFontSize);
    settings_.setValue(QStringLiteral("overlay/originalFontSize"), appearance.originalFontSize);
    settings_.setValue(QStringLiteral("overlay/backgroundOpacity"), appearance.backgroundOpacity);
    settings_.setValue(QStringLiteral("overlay/showTranslation"), appearance.showTranslation);
    settings_.setValue(QStringLiteral("overlay/showOriginal"), appearance.showOriginal);
    settings_.sync();
    if (changed) emit overlayAppearanceChanged();
}

void SettingsManager::setOverlayClickThrough(bool enabled)
{
    settings_.setValue(QStringLiteral("overlay/clickThrough"), enabled);
    settings_.sync();
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
    const QString before = sourceLanguage();
    writeValidated(kSourceLanguageKey, languageId, kSourceLanguages, kDefaultSourceLanguage);
    if (before != sourceLanguage()) emit translationSettingsChanged();
}

void SettingsManager::setTargetLanguage(const QString &languageId)
{
    const QString before = targetLanguage();
    writeValidated(kTargetLanguageKey, languageId, kTargetLanguages, kDefaultTargetLanguage);
    if (before != targetLanguage()) emit translationSettingsChanged();
}

void SettingsManager::setOcrEngine(const QString &engineId)
{
    const QString before = ocrEngine();
    writeValidated(kOcrEngineKey, engineId, kOcrEngines, kDefaultOcrEngine);
    if (before != ocrEngine()) emit translationSettingsChanged();
}

void SettingsManager::setTranslator(const QString &translatorId)
{
    const QString before = translator();
    writeValidated(kTranslatorKey, translatorId, kTranslators, kDefaultTranslator);
    if (before != translator()) emit translationSettingsChanged();
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
