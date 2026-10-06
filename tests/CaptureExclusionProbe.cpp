#include "config/SettingsManager.h"
#include "gui/TranslationWindow.h"
#include "capture/ScreenCaptureService.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QScreen>
#include <QTemporaryDir>
#include <QTimer>
#include <QEventLoop>
#include <QTextStream>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// Explicit desktop QA, never packaged or run as an offscreen unit test.
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setOrganizationName(QStringLiteral("TranslatorCaptureQA"));
    app.setApplicationName(QStringLiteral("CaptureExclusion"));
    if ((app.arguments().size() != 2 && app.arguments().size() != 3)
        || app.platformName() == "offscreen" || !app.primaryScreen()) return 2;
    const bool observe = app.arguments().contains(QStringLiteral("--observe"));
    QDir output(app.arguments()[1]);
    if (!output.exists() && !QDir().mkpath(output.absolutePath())) return 2;
    QTemporaryDir temporary;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    SettingsManager settings;
    ScreenCaptureService capture;
    QScreen *screen = app.primaryScreen();
    auto settle = [&] {
        QEventLoop loop; QTimer::singleShot(650, &loop, &QEventLoop::quit); loop.exec();
    };
    QLabel lower(QStringLiteral("UNDERLYING CAPTURE TARGET\nThis background must remain visible in OCR input."));
    lower.setWindowTitle(QStringLiteral("Translator Capture QA Background"));
    lower.setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    lower.setAlignment(Qt::AlignCenter);
    lower.setStyleSheet(QStringLiteral("background: #217357; color: white; font-size: 24px;"));
    lower.setGeometry(QRect(screen->availableGeometry().topLeft() + QPoint(80, 100), QSize(900, 380)));
    lower.show(); lower.raise(); lower.activateWindow(); settle();
    if (observe) {
        QEventLoop loop; QTimer::singleShot(30000, &loop, &QEventLoop::quit); loop.exec();
    }
    const QRect region(lower.pos() + QPoint(30, 70), QSize(820, 230));
    const auto baseline = capture.capture(screen, region);
    if (!baseline.isValid()) return 2;
    baseline.image.save(output.filePath(QStringLiteral("baseline.png")));
    qint64 backgroundPixels = 0;
    for (int y = 0; y < baseline.image.height(); ++y) for (int x = 0; x < baseline.image.width(); ++x) {
        const auto color = baseline.image.pixelColor(x, y);
        if (qAbs(color.red()-33) + qAbs(color.green()-115) + qAbs(color.blue()-87) < 30) ++backgroundPixels;
    }
    const bool baselineValid = double(backgroundPixels) / (baseline.image.width()*baseline.image.height()) > 0.5;
    TranslationWindow window(settings);
    auto appearance = settings.overlayAppearance();
    appearance.translationFontSize = 30; appearance.originalFontSize = 18; appearance.backgroundOpacity = 80;
    settings.setOverlayAppearance(appearance);
    window.setTranslatedText(QStringLiteral("TRANSLATOR OVERLAY TEST"));
    window.setOriginalText(QStringLiteral("Overlay must not appear in production capture"));
    window.setGeometry(region.adjusted(20, 15, -20, -15));
    window.show(); settle();
    if (observe) {
        QEventLoop loop; QTimer::singleShot(30000, &loop, &QEventLoop::quit); loop.exec();
    }
    QJsonArray stages;
    bool apiPass = true, onPass = true, offPass = true;
    auto take = [&](const QString &name, bool expectedExcluded) {
        settle();
        const auto shot = capture.capture(screen, region);
        qint64 changed = 0;
        if (shot.isValid() && shot.image.size() == baseline.image.size()) {
            for (int y = 0; y < shot.image.height(); ++y) for (int x = 0; x < shot.image.width(); ++x) {
                const QColor a = shot.image.pixelColor(x, y), b = baseline.image.pixelColor(x, y);
                if (qAbs(a.red()-b.red()) + qAbs(a.green()-b.green()) + qAbs(a.blue()-b.blue()) > 30) ++changed;
            }
        } else changed = -1;
        const double difference = changed >= 0 ? double(changed) / (baseline.image.width()*baseline.image.height()) : 1;
        const bool applied = window.captureExclusionStatus() == WindowCaptureExclusion::Status::Applied;
        quintptr native = quintptr(window.effectiveWinId());
        unsigned long affinity = 0;
        bool readback = false;
#ifdef Q_OS_WIN
        DWORD flag = 0;
        readback = GetWindowDisplayAffinity(reinterpret_cast<HWND>(native), &flag);
        affinity = flag;
#endif
        const bool api = applied && readback && affinity == (expectedExcluded ? 0x11UL : 0UL);
        const bool pass = baselineValid && shot.isValid() && (expectedExcluded ? difference < 0.005 : difference > 0.05);
        apiPass = apiPass && api;
        if (expectedExcluded) onPass = onPass && pass; else offPass = offPass && pass;
        if (shot.isValid()) shot.image.save(output.filePath(name + ".png"));
        stages.append(QJsonObject{{"stage", name}, {"excluded", expectedExcluded}, {"api_pass", api},
            {"affinity", int(affinity)}, {"hwnd", QString::number(native)}, {"overlay_visible", window.isVisible()},
            {"changed_fraction", difference}, {"capture_pass", pass}, {"capture_error", shot.error}});
    };
    take(QStringLiteral("interactive-on"), true);
    window.grab().save(output.filePath(QStringLiteral("overlay-render.png")));
    window.setInteractionMode(OverlayInteractionMode::ClickThrough);
    take(QStringLiteral("clickthrough-on"), true);
    QJsonArray handles;
    for (int i = 0; i < 20; ++i) {
        window.setInteractionMode(i % 2 ? OverlayInteractionMode::ClickThrough : OverlayInteractionMode::Interactive);
        app.processEvents(); handles.append(QString::number(quintptr(window.effectiveWinId())));
    }
    take(QStringLiteral("after-20-switches-on"), true);
    settings.setOverlayExcludeFromCapture(false);
    take(QStringLiteral("clickthrough-off"), false);
    window.setInteractionMode(OverlayInteractionMode::Interactive);
    take(QStringLiteral("interactive-off"), false);
    window.close(); lower.close();
    QJsonObject report{{"platform", app.platformName()}, {"qt", qVersion()}, {"screen", screen->name()},
        {"dpr", screen->devicePixelRatio()}, {"capture_path", "ScreenCaptureService / QScreen::grabWindow"},
        {"baseline_valid", baselineValid},
        {"stages", stages}, {"toggle_handles", handles}, {"api_pass", apiPass}, {"on_pass", onPass},
        {"off_pass", offPass}, {"production_capture_pass", apiPass && onPass && offPass},
        {"visual_evidence", "overlay-render.png is QWidget rendering, not a physical monitor observation"}};
    QFile file(output.filePath(QStringLiteral("report.json")));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0) return 2;
    QTextStream(stdout) << QJsonDocument(report).toJson() << Qt::endl;
    return apiPass && onPass && offPass ? 0 : 1;
}
