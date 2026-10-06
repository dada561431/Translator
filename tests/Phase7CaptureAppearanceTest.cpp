#include "config/SettingsManager.h"
#include "gui/TranslationWindow.h"
#include "gui/SettingsDialog.h"
#include "platform/WindowCaptureExclusion.h"
#include "app/RealtimePipelineCoordinator.h"
#include "app/OcrCoordinator.h"
#include "app/TranslationCoordinator.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <iostream>
#include <limits>

namespace {
int failures = 0;
void check(bool ok, const char *message)
{
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool until(const std::function<bool()> &ready)
{
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 3000) {
        QApplication::processEvents(); QThread::msleep(2);
    }
    return ready();
}
class Engine final : public IOcrEngine
{
public:
    QString id() const override { return QStringLiteral("test"); }
    OcrResult recognize(const QImage &image, const QString &) override
    {
        OcrResult result; result.text = QStringLiteral("frame %1").arg(image.pixelColor(0, 0).red());
        return result;
    }
};
class Translator final : public ITranslator
{
public:
    QString id() const override { return QStringLiteral("test"); }
    void translate(const TranslationRequest &request) override
    {
        TranslationResult result; result.requestId = request.requestId;
        result.success = true; result.translatedText = request.sourceText + QStringLiteral(" translated");
        emit resultReady(result);
    }
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setOrganizationName(QStringLiteral("TranslatorPhase7BTests"));
    app.setApplicationName(QStringLiteral("CaptureAppearance"));
    QTemporaryDir temporary;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    SettingsManager settings;
#ifdef Q_OS_WIN
    check(settings.overlayExcludeFromCapture(), "Windows default exclusion ON");
#else
    check(!settings.overlayExcludeFromCapture(), "non-Windows default exclusion OFF");
#endif
    const auto defaults = settings.overlayAppearance();
    check(defaults.translationFontSize == qMax(16.0, app.font().pointSizeF() + 6)
          && defaults.originalFontSize == qMax(12.0, app.font().pointSizeF() + 2)
          && defaults.backgroundOpacity == 0 && defaults.showOriginal && defaults.showTranslation,
          "existing default fonts/transparency/visibility retained");
    int apiCalls = 0; quintptr handle = 0; bool excluded = false, supported = true, fail = false;
    WindowCaptureExclusion::Backend backend{[&] { return supported; },
        [&](quintptr value, bool flag, QString &error) {
            ++apiCalls; handle = value; excluded = flag;
            if (fail) error = QStringLiteral("fake native failure 123");
            return !fail;
        }};
    WindowCaptureExclusion boundary(backend);
    check(!boundary.setExcludedHandle(0, true) && apiCalls == 0
          && boundary.status() == WindowCaptureExclusion::Status::AwaitingWindow, "no HWND safe/deferred");
    check(boundary.setExcludedHandle(11, true) && handle == 11 && excluded, "ON native route");
    check(boundary.setExcludedHandle(22, false) && handle == 22 && !excluded, "new fake HWND OFF route");
    supported = false;
    const int calls = apiCalls;
    check(!boundary.setExcludedHandle(22, true) && apiCalls == calls
          && boundary.status() == WindowCaptureExclusion::Status::Unsupported, "unsupported no native call");
    supported = true; fail = true;
    check(!boundary.setExcludedHandle(22, true) && !boundary.lastError().isEmpty()
          && boundary.status() == WindowCaptureExclusion::Status::Failed, "native failure explicit/safe");
    fail = false;
    TranslationWindow window(settings, nullptr, nullptr, backend);
    check(!window.effectiveWinId(), "construction does not force native window");
    window.show(); app.processEvents();
    check(window.captureExclusionStatus() == WindowCaptureExclusion::Status::Applied && excluded,
          "show applies exclusion");
    const int beforeEvent = apiCalls;
    QEvent recreated(QEvent::WinIdChange); QApplication::sendEvent(&window, &recreated);
    check(apiCalls > beforeEvent, "WinIdChange reapplies");
    for (int i = 0; i < 20; ++i) {
        const int before = apiCalls;
        window.setInteractionMode(i % 2 ? OverlayInteractionMode::Interactive : OverlayInteractionMode::ClickThrough);
        app.processEvents();
        check(apiCalls > before && excluded && window.isVisible(), "20 flag changes reapply and retain visibility");
    }
    settings.setOverlayExcludeFromCapture(false);
    check(!excluded && !SettingsManager().overlayExcludeFromCapture(), "OFF apply and persistence");
    settings.setOverlayExcludeFromCapture(true);
    check(excluded && SettingsManager().overlayExcludeFromCapture(), "ON apply and persistence");
    fail = true; QApplication::sendEvent(&window, &recreated);
    auto *dialog = static_cast<SettingsDialog *>(window.findChild<QDialog *>("settingsDialog"));
    check(!dialog->findChild<QCheckBox *>("excludeFromCaptureCheck")->isEnabled()
          && dialog->findChild<QLabel *>("captureExclusionNote")->text() == "Capture exclusion unavailable",
          "failure disables checkbox with no native codes in UI");
    fail = false; QApplication::sendEvent(&window, &recreated);
    check(dialog->findChild<QCheckBox *>("excludeFromCaptureCheck")->isEnabled(), "successful retry recovers UI");
    int translationSignals = 0, appearanceSignals = 0;
    QObject::connect(&settings, &SettingsManager::translationSettingsChanged, &app, [&] { ++translationSignals; });
    QObject::connect(&settings, &SettingsManager::overlayAppearanceChanged, &app, [&] { ++appearanceSignals; });
    auto *translated = window.findChild<QLabel *>("translatedLabel");
    auto *original = window.findChild<QLabel *>("originalLabel");
    const QPoint position = window.pos();
    auto appearance = defaults;
    appearance.translationFontSize = 24; appearance.originalFontSize = 18;
    appearance.backgroundOpacity = 65;
    settings.setOverlayAppearance(appearance);
    check(translated->font().pointSizeF() == 24 && original->font().pointSizeF() == 18,
          "independent fonts applied");
    check(window.windowOpacity() == 1.0 && window.pos() == position, "text/window opacity and position unchanged");
    auto *area = window.findChild<QWidget *>("subtitleArea");
    check(area->styleSheet().contains("166"), "container alpha only");
    // Render the whole translucent top-level: child grab also paints its
    // inherited background and would composite the same alpha twice.
    const auto renderedArea = window.grab().toImage();
    const QPoint backgroundPoint = area->mapTo(&window, QPoint(5, area->height() - 5));
    check(qAbs(renderedArea.pixelColor(backgroundPoint).alpha() - 166) <= 1,
          "actual background paint alpha matches percentage");
    bool opaqueText = false;
    for (int y = 0; y < renderedArea.height(); ++y) for (int x = 0; x < renderedArea.width(); ++x) {
        const auto color = renderedArea.pixelColor(x, y);
        if (color.red() > 240 && color.green() > 240 && color.blue() > 240 && color.alpha() > 250) opaqueText = true;
    }
    check(opaqueText, "subtitle glyphs remain opaque with translucent background");
    const QString style = area->styleSheet();
    window.setOriginalText("latest original"); window.setTranslatedText("latest translation");
    check(area->styleSheet() == style, "content updates do not rebuild style");
    check(SettingsManager().overlayAppearance() == appearance, "appearance persists across managers");
    dialog->show(); app.processEvents();
    auto *font = dialog->findChild<QDoubleSpinBox *>("translationFontSizeSpin");
    auto *opacity = dialog->findChild<QSpinBox *>("backgroundOpacitySpin");
    auto *showOriginal = dialog->findChild<QCheckBox *>("showOriginalCheck");
    auto *showTranslation = dialog->findChild<QCheckBox *>("showTranslationCheck");
    font->setValue(30); opacity->setValue(40); showOriginal->click();
    check(!showTranslation->isEnabled() && showOriginal->isEnabled(), "last visible field cannot be unchecked");
    check(settings.overlayAppearance() == appearance, "drafts not live-saved");
    dialog->findChild<QDialogButtonBox *>("settingsButtonBox")->button(QDialogButtonBox::Apply)->click();
    check(translated->font().pointSizeF() == 30 && original->isHidden()
          && settings.overlayAppearance().backgroundOpacity == 40, "Apply updates and saves appearance");
    check(translationSignals == 0, "appearance-only Apply does not create provider keys/translation signals");
    font->setValue(50); dialog->reject();
    dialog->show(); app.processEvents();
    check(font->value() == 30 && settings.overlayAppearance().translationFontSize == 30, "Cancel discards unapplied draft");
    dialog->reject();
    check(appearanceSignals == 2, "dedicated appearance signals only");
    supported = false; QApplication::sendEvent(&window, &recreated);
    check(!dialog->findChild<QCheckBox *>("excludeFromCaptureCheck")->isEnabled(), "unsupported disables capture UI");
    supported = true; QApplication::sendEvent(&window, &recreated);
    QSettings raw;
    raw.setValue("overlay/translationFontSize", 9999);
    raw.setValue("overlay/originalFontSize", -50);
    raw.setValue("overlay/backgroundOpacity", -50);
    raw.setValue("overlay/showOriginal", false); raw.setValue("overlay/showTranslation", false); raw.sync();
    const auto safe = SettingsManager().overlayAppearance();
    check(safe.translationFontSize == 72 && safe.originalFontSize == 10 && safe.backgroundOpacity == 0
          && safe.showTranslation, "invalid stored values and double-false sanitized");
    appearance.translationFontSize = std::numeric_limits<double>::quiet_NaN();
    appearance.originalFontSize = 9999; appearance.backgroundOpacity = 9999;
    appearance.showTranslation = false; appearance.showOriginal = false;
    settings.setOverlayAppearance(appearance);
    check(settings.overlayAppearance().translationFontSize == 16 && settings.overlayAppearance().originalFontSize == 72
          && settings.overlayAppearance().backgroundOpacity == 100 && settings.overlayAppearance().showTranslation,
          "setter also clamps/falls back and prevents both hidden");
    settings.setOverlayAppearance(defaults);
    window.close();
    {
        TranslationWindow restarted(settings, nullptr, nullptr, backend);
        restarted.show(); app.processEvents();
        check(excluded && restarted.findChild<QLabel *>("originalLabel")->font().pointSizeF() == defaults.originalFontSize,
              "new window restores saved appearance and ON affinity");
        restarted.close();
    }
    settings.setOverlayExcludeFromCapture(false);
    {
        TranslationWindow restarted(settings, nullptr, nullptr, backend);
        restarted.show(); app.processEvents();
        check(!excluded, "new window restores OFF affinity"); restarted.close();
    }
    // Real coordinators with controlled engines verify no lifecycle invalidation.
    settings.setTranslator(QStringLiteral("deepl"));
    settings.setSourceLanguage(QStringLiteral("en"));
    settings.setTargetLanguage(QStringLiteral("zh"));
    settings.setCaptureRegion(QRect(app.primaryScreen()->geometry().topLeft(), QSize(160, 50)), app.primaryScreen()->name());
    std::atomic<int> engines{0}; int translators = 0, pixel = 0;
    OcrCoordinator ocr([&] { ++engines; return std::make_unique<Engine>(); });
    TranslationCoordinator translation(settings, [&]() -> std::unique_ptr<ITranslator> {
        ++translators; return std::make_unique<Translator>();
    });
    RealtimePipelineCoordinator pipeline(settings, ocr, translation, nullptr,
        [&](const QRect &region, const QString &screen) {
            CaptureResult capture; capture.globalRect = region;
            capture.screenName = screen.isEmpty() ? QStringLiteral("offscreen-fixture") : screen;
            capture.image = QImage(160, 50, QImage::Format_RGB32); capture.image.fill(QColor(pixel, pixel, pixel));
            return capture;
        }, RealtimeOptions{100000, 2, 4});
    QObject::connect(&pipeline, &RealtimePipelineCoordinator::originalTextReady, &window, &TranslationWindow::setOriginalText);
    QObject::connect(&translation, &TranslationCoordinator::resultReady, &window, [&](const TranslationResult &result) {
        if (result.success) window.setTranslatedText(result.translatedText);
    });
    window.show(); check(pipeline.start(), "pipeline starts");
    check(until([&] { return translated->text() == "frame 0 translated"; }), "initial OCR/translation update");
    if (translated->text() != "frame 0 translated")
        std::cerr << "Diagnostic: original=" << original->text().toStdString()
                  << " translation=" << translated->text().toStdString()
                  << " engine_count=" << engines << " provider=" << settings.translator().toStdString() << '\n';
    const auto session = pipeline.sessionId(); const int initialEngines = engines, initialTranslators = translators;
    auto live = defaults; live.showOriginal = false; live.backgroundOpacity = 30; live.translationFontSize = 28;
    dialog->show(); app.processEvents();
    const int beforeLiveApply = translationSignals;
    font->setValue(live.translationFontSize); opacity->setValue(live.backgroundOpacity); showOriginal->click();
    dialog->findChild<QDialogButtonBox *>("settingsButtonBox")->button(QDialogButtonBox::Apply)->click();
    check(translationSignals == beforeLiveApply && pipeline.sessionId() == session
          && translators == initialTranslators, "live DeepL-provider appearance Apply does not invalidate/recreate backend");
    dialog->reject();
    window.setInteractionMode(OverlayInteractionMode::ClickThrough);
    pixel = 80; pipeline.tick();
    check(until([&] { return translated->text() == "frame 80 translated"; }) && original->isHidden()
          && original->text() == "frame 80", "hidden Original keeps latest OCR and translation continues");
    live.showOriginal = true; live.showTranslation = false; settings.setOverlayAppearance(live);
    pixel = 160; pipeline.tick();
    check(until([&] { return translated->text() == "frame 160 translated"; }) && translated->isHidden()
          && original->text() == "frame 160", "hidden Translation keeps latest content and OCR continues");
    live.showTranslation = true; settings.setOverlayAppearance(live);
    check(translated->isVisible() && translated->text() == "frame 160 translated" && original->isVisible(),
          "reshow latest both fields");
    check(pipeline.isRunning() && pipeline.sessionId() == session && engines == initialEngines
          && translators == initialTranslators, "appearance/input mode do not stop/restart engines or translator");
    pipeline.stop(); window.close();
    std::cout << "Phase 7B.1 checks: " << failures << " failures\n";
    return failures ? 1 : 0;
}
