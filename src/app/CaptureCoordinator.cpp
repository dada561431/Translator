#include "app/CaptureCoordinator.h"

#include "config/SettingsManager.h"
#include "gui/TranslationWindow.h"

#include <QDebug>
#include <QDir>
#include <QPointer>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

CaptureCoordinator::CaptureCoordinator(TranslationWindow &window,
                                       SettingsManager &settings, QObject *parent)
    : QObject(parent)
    , window_(window)
    , settings_(settings)
    , selector_(this)
{
    connect(&window_, &TranslationWindow::regionSelectionRequested,
            this, [this] { beginSelection(); });
    connect(&selector_, &RegionSelector::regionSelected, this,
            [this](const QRect &region, QScreen *screen) {
                QPointer<QScreen> selectedScreen(screen);
                if (selectedScreen) {
                    settings_.setCaptureRegion(region, selectedScreen->name());
                }
                QTimer::singleShot(150, this, [this, region, selectedScreen] {
                    captureSelectedRegion(region, selectedScreen);
                });
            });
    connect(&selector_, &RegionSelector::selectionCanceled, this, [this] {
        qDebug() << "Region selection canceled; previous region kept.";
        restoreWindow(QStringLiteral("Region selection canceled."));
    });

    const QRect savedRegion = settings_.captureRegion();
    if (savedRegion.isValid()) {
        window_.setRegionFeedback(
            QStringLiteral("Last region: %1 x %2")
                .arg(savedRegion.width()).arg(savedRegion.height()));
    }
}

void CaptureCoordinator::beginSelection()
{
    if (selecting_) {
        return;
    }

    selecting_ = true;
    window_.hide();
    QTimer::singleShot(80, this, [this] { selector_.start(); });
}

void CaptureCoordinator::captureSelectedRegion(const QRect &region, QScreen *screen)
{
    const CaptureResult result = captureService_.capture(screen, region);
    if (!result.isValid()) {
        qWarning() << "Screen capture failed:" << result.error;
        restoreWindow(QStringLiteral("Screen capture failed."));
        return;
    }

    QString debugPath;
#ifndef NDEBUG
    const QString debugDirectory = QStandardPaths::writableLocation(
        QStandardPaths::TempLocation) + QStringLiteral("/Translator");
    if (QDir().mkpath(debugDirectory)) {
        debugPath = debugDirectory + QStringLiteral("/last_capture.png");
        if (!result.image.save(debugPath, "PNG")) {
            qWarning() << "Could not save debug capture:" << debugPath;
            debugPath.clear();
        }
    } else {
        qWarning() << "Could not create debug capture directory:" << debugDirectory;
    }
#endif
    qDebug() << "Capture region (global logical):" << result.globalRect
             << "screen:" << result.screenName
             << "DPR:" << result.devicePixelRatio
             << "image pixels:" << result.image.size()
             << "debug path:" << debugPath;

    restoreWindow(QStringLiteral("Region selected: %1 x %2")
                      .arg(region.width()).arg(region.height()));
}

void CaptureCoordinator::restoreWindow(const QString &feedback)
{
    selecting_ = false;
    window_.show();
    window_.setRegionFeedback(feedback);
}
