#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>

#include <iostream>

#include "capture/RegionSelector.h"
#include "capture/ScreenCaptureService.h"
#include "config/SettingsManager.h"

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void sendMouse(QWidget *widget, QEvent::Type type, const QPoint &globalPosition,
               Qt::MouseButton button, Qt::MouseButtons buttons)
{
    const QPoint local = widget->mapFromGlobal(globalPosition);
    QMouseEvent event(type, QPointF(local), QPointF(globalPosition), button, buttons,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

} // namespace

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TranslatorPhase3Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("CaptureTest"));

    QTemporaryDir directory;
    check(directory.isValid(), "temporary settings directory exists");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());

    const QRect reversed = RegionSelector::normalizedRegion(QPoint(500, 400),
                                                              QPoint(100, 100));
    check(reversed == QRect(100, 100, 400, 300), "reverse drag normalizes to logical width and height");
    check(RegionSelector::isValidRegion(reversed), "normal region is valid");
    check(!RegionSelector::isValidRegion(QRect(0, 0, 9, 100)), "narrow region is rejected");
    check(!RegionSelector::isValidRegion(QRect(0, 0, 100, 9)), "short region is rejected");

    const QRect leftScreen(-1920, 0, 1920, 1080);
    const QRect globalRegion(-1500, 200, 500, 300);
    check(ScreenCaptureService::toScreenLocalRect(globalRegion, leftScreen)
              == QRect(420, 200, 500, 300), "negative-origin screen converts to local coordinates");
    check(ScreenCaptureService::isRegionValid(globalRegion, leftScreen),
          "region fully inside a negative-origin screen is valid");
    check(!ScreenCaptureService::isRegionValid(QRect(-100, 200, 500, 300), leftScreen),
          "region crossing a screen edge is rejected");
    ScreenCaptureService captureService;
    check(!captureService.capture(nullptr, globalRegion).isValid(),
          "missing screen returns an invalid capture");

    QScreen *screen = QGuiApplication::primaryScreen();
    check(screen != nullptr, "test platform provides a screen");
    if (!screen) {
        return 1;
    }
    std::cout << "Screen " << screen->name().toStdString()
              << " geometry " << screen->geometry().x() << ',' << screen->geometry().y()
              << ' ' << screen->geometry().width() << 'x' << screen->geometry().height()
              << " DPR " << screen->devicePixelRatio() << '\n';

    const QRect persistedRegion(screen->geometry().topLeft() + QPoint(20, 20), QSize(80, 60));
    if (qEnvironmentVariableIsSet("TRANSLATOR_CAPTURE_SMOKE")) {
        const CaptureResult result = captureService.capture(screen, persistedRegion);
        check(result.isValid(), "native single-frame capture returns a valid QImage");
        if (result.isValid()) {
            std::cout << "Capture logical " << persistedRegion.width() << 'x'
                      << persistedRegion.height() << " image pixels "
                      << result.image.width() << 'x' << result.image.height()
                      << " screen DPR " << result.devicePixelRatio << '\n';
        }
    }
    {
        SettingsManager settings;
        settings.setCaptureRegion(persistedRegion, screen->name());
    }
    {
        SettingsManager settings;
        check(settings.captureRegion() == persistedRegion, "capture region persists across instances");
        check(settings.captureScreen() == screen->name(), "capture screen persists across instances");
    }
    {
        QSettings raw;
        raw.setValue(QStringLiteral("capture/region"), QRect(50000, 50000, 80, 60));
        raw.sync();
        SettingsManager settings;
        check(!settings.captureRegion().isValid(), "off-screen persisted region becomes invalid");
        settings.setCaptureRegion(persistedRegion, QStringLiteral("missing-screen"));
        check(!settings.captureRegion().isValid(), "missing saved screen becomes invalid");
    }

    RegionSelector selector;
    int selected = 0;
    int canceled = 0;
    QRect emittedRect;
    QScreen *emittedScreen = nullptr;
    QObject::connect(&selector, &RegionSelector::regionSelected, &application,
                     [&](const QRect &region, QScreen *selectedScreen) {
                         ++selected;
                         emittedRect = region;
                         emittedScreen = selectedScreen;
                     });
    QObject::connect(&selector, &RegionSelector::selectionCanceled,
                     &application, [&] { ++canceled; });

    selector.start();
    application.processEvents();
    QPointer<QWidget> overlay;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("regionSelectionOverlay")
            && widget->isVisible()) {
            overlay = widget;
            break;
        }
    }
    check(selector.isActive() && overlay && overlay->isVisible(), "selection overlay opens");
    if (overlay) {
        const QPoint first = overlay->geometry().topLeft() + QPoint(160, 140);
        const QPoint second = overlay->geometry().topLeft() + QPoint(40, 30);
        sendMouse(overlay, QEvent::MouseButtonPress, first, Qt::LeftButton, Qt::LeftButton);
        sendMouse(overlay, QEvent::MouseMove, second, Qt::NoButton, Qt::LeftButton);
        sendMouse(overlay, QEvent::MouseButtonRelease, second, Qt::LeftButton, Qt::NoButton);
        application.processEvents();
        check(selected == 1 && emittedScreen == screen, "reverse selection emits source screen");
        check(emittedRect == QRect(second, QSize(120, 110)),
              "selection emits normalized global logical QRect");
        check(!selector.isActive() && (!overlay || !overlay->isVisible()),
              "overlay hides after selection");
    }

    selector.start();
    application.processEvents();
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("regionSelectionOverlay") && widget->isVisible()) {
            overlay = widget;
            break;
        }
    }
    if (overlay) {
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(overlay, &escape);
        application.processEvents();
        check(canceled == 1 && !selector.isActive(), "Escape cancels selection");
    }

    selector.start();
    application.processEvents();
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("regionSelectionOverlay") && widget->isVisible()) {
            overlay = widget;
            break;
        }
    }
    if (overlay) {
        const QPoint first = overlay->geometry().topLeft() + QPoint(20, 20);
        sendMouse(overlay, QEvent::MouseButtonPress, first, Qt::LeftButton, Qt::LeftButton);
        sendMouse(overlay, QEvent::MouseButtonRelease, first + QPoint(4, 5),
                  Qt::LeftButton, Qt::NoButton);
        application.processEvents();
        check(canceled == 2 && selected == 1, "tiny selection cancels without a new region");
    }

    if (failures == 0) {
        std::cout << "All Phase 3 capture logic checks passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
