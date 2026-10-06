#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "app/CaptureCoordinator.h"
#include "app/OcrCoordinator.h"
#include "app/TranslationCoordinator.h"
#include "app/RealtimePipelineCoordinator.h"
#include "app/RuntimeSelfCheck.h"
#include "app/GlobalShortcutManager.h"
#include "app/OverlayInteractionController.h"
#include "config/SettingsManager.h"
#include "gui/TranslationWindow.h"
#include "ocr/OcrEngineFactory.h"
#include "translator/TranslatorFactory.h"
#include "credentials/ICredentialStore.h"

#include <memory>
#include <iterator>

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
    const QDir appDir(QFileInfo(QString::fromWCharArray(path, int(length))).absolutePath());
    if (length && length < std::size(path) && (appDir.exists(QStringLiteral("runtime-manifest.json"))
                                             || appDir.exists(QStringLiteral("ocr")))) {
        qunsetenv("QT_PLUGIN_PATH");
        qunsetenv("QT_QPA_PLATFORM_PLUGIN_PATH");
    }
#endif
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TranslatorProject"));
    QApplication::setApplicationName(QStringLiteral("Translator"));
    if (application.arguments().contains(QStringLiteral("--self-check"))) {
        const int reportIndex = application.arguments().indexOf(QStringLiteral("--report"));
        return runRuntimeSelfCheck(reportIndex >= 0 ? application.arguments().value(reportIndex + 1) : QString());
    }

    SettingsManager settings;
    auto credentials = createPlatformCredentialStore();
    TranslationWindow translationWindow(settings, nullptr, credentials.get());
    CaptureCoordinator captureCoordinator(translationWindow, settings);
    const auto helperOptions = PaddleHelperOptions::fromEnvironment();
    OcrCoordinator ocrCoordinator({}, nullptr, [helperOptions](const QString &engineId) {
        return OcrEngineFactory::create(engineId, helperOptions);
    });
    TranslationCoordinator translationCoordinator(settings, [&] {
        return TranslatorFactory::create(settings, *credentials);
    });
    RealtimePipelineCoordinator realtime(settings, ocrCoordinator, translationCoordinator);
    QObject::connect(&translationWindow, &TranslationWindow::startRequested,
                     &realtime, [&] { realtime.start(); });
    QObject::connect(&translationWindow, &TranslationWindow::stopRequested,
                     &realtime, &RealtimePipelineCoordinator::stop);
    QObject::connect(&captureCoordinator, &CaptureCoordinator::selectionStarted,
                     &realtime, &RealtimePipelineCoordinator::stop);
    QObject::connect(&realtime, &RealtimePipelineCoordinator::runningChanged,
                     &translationWindow, &TranslationWindow::setTranslationRunning);
    QObject::connect(&realtime, &RealtimePipelineCoordinator::feedback,
                     &translationWindow, &TranslationWindow::setRegionFeedback);
    QObject::connect(&realtime, &RealtimePipelineCoordinator::originalTextReady,
                     &translationWindow, &TranslationWindow::setOriginalText);
    QObject::connect(&realtime, &RealtimePipelineCoordinator::subtitlesCleared,
                     &translationWindow, [&] { translationWindow.setOriginalText(QString()); });
    QObject::connect(&translationCoordinator, &TranslationCoordinator::stateChanged,
                     &translationWindow, &TranslationWindow::setTranslationState);
    QObject::connect(&translationCoordinator, &TranslationCoordinator::resultReady,
                     &translationWindow, [&translationWindow](const TranslationResult &result) {
                         if (result.success) translationWindow.setTranslatedText(result.translatedText);
#ifndef NDEBUG
                         qDebug() << "[Translation] UI update requestId=" << result.requestId
                                  << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
#endif
                     });
    QObject::connect(&captureCoordinator, &CaptureCoordinator::captureCompleted,
                     &realtime, &RealtimePipelineCoordinator::acceptOneShot);
    translationWindow.show();
    GlobalShortcutManager shortcuts;
    OverlayInteractionController overlayInteraction(translationWindow, settings, shortcuts, {
        [&] { return captureCoordinator.isSelecting(); },
        [&] { emit translationWindow.regionSelectionRequested(); },
        [&] { return realtime.isRunning(); },
        [&] { realtime.start(); },
        [&] { realtime.stop(); }
    });
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &shortcuts,
                     &GlobalShortcutManager::unregisterAll);
    overlayInteraction.initialize();

    return application.exec();
}
