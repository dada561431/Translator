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
#include "gui/OverlayTrayController.h"
#include "ocr/OcrEngineFactory.h"
#include "translator/TranslatorFactory.h"
#include "credentials/ICredentialStore.h"
#include "app/ProductionInputController.h"
#include "gui/SettingsDialog.h"
#ifdef TRANSLATOR_HAS_WHISPER
#include "asr/WhisperCppAsrBackend.h"
#endif

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
    application.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName(QStringLiteral("TranslatorProject"));
    QApplication::setApplicationName(QStringLiteral("Translator"));
    if (application.arguments().contains(QStringLiteral("--self-check"))) {
        const int reportIndex = application.arguments().indexOf(QStringLiteral("--report"));
        return runRuntimeSelfCheck(reportIndex >= 0 ? application.arguments().value(reportIndex + 1) : QString());
    }

    SettingsManager settings;
    auto credentials = createPlatformCredentialStore();
    TranslationWindow translationWindow(settings, nullptr, credentials.get());
    CaptureCoordinator captureCoordinator(translationWindow, settings, nullptr, false);
    const auto helperOptions = PaddleHelperOptions::fromEnvironment();
    OcrCoordinator ocrCoordinator({}, nullptr, [helperOptions](const QString &engineId) {
        return OcrEngineFactory::create(engineId, helperOptions);
    });
    TranslationCoordinator translationCoordinator(settings, [&] {
        return TranslatorFactory::create(settings, *credentials);
    });
    RealtimePipelineCoordinator realtime(settings, ocrCoordinator, translationCoordinator);
    AudioInputCoordinator audioCapture;
#ifdef TRANSLATOR_HAS_WHISPER
    AsrCoordinator asr([] { return std::make_unique<WhisperCppAsrBackend>(); });
    constexpr bool speechAvailable = true;
#else
    AsrCoordinator asr({});
    constexpr bool speechAvailable = false;
#endif
    AudioTranslationCoordinator audio(audioCapture, asr, translationCoordinator);
    if (qEnvironmentVariableIntValue("TRANSLATOR_AUDIO_DIAGNOSTICS") == 1) {
        QObject::connect(&audio, &AudioTranslationCoordinator::latencyMeasured, &application,
            [](quint64 session, quint64 utterance, const QString &stage, qint64 end, qint64 boundary, qint64 now, qint64 processing) {
                qInfo() << "[Audio QA]" << session << utterance << stage
                        << "speech_end_ms" << (now - end) / 1000 << "boundary_ms" << (now - boundary) / 1000
                        << "processing_ms" << processing;
            });
        QObject::connect(&audio, &AudioTranslationCoordinator::translationRequested, &application,
            [](quint64 session, quint64 utterance, const TranslationRequest &) { qInfo() << "[Audio QA] translation request" << session << utterance; });
    }
    ProductionInputController input(settings, audioCapture, asr, audio,
        {[&] { realtime.start(); }, [&] { realtime.stop(); }, [&] { return realtime.isRunning(); }}, speechAvailable);
    translationWindow.settingsDialog()->setAudioDevices([&](auto kind) { return audioCapture.devices(kind); });
    QObject::connect(&input, &ProductionInputController::stateChanged, &translationWindow, &TranslationWindow::setTranslationRunning);
    QObject::connect(&input, &ProductionInputController::feedback, &translationWindow, &TranslationWindow::setRegionFeedback);
    QObject::connect(&audio, &AudioTranslationCoordinator::originalTextReady, &translationWindow, &TranslationWindow::setOriginalText);
    QObject::connect(&translationWindow, &TranslationWindow::stopRequested,
                     &input, &ProductionInputController::stop);
    QObject::connect(&captureCoordinator, &CaptureCoordinator::selectionStarted,
                     &input, &ProductionInputController::stop);
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
                     &realtime, [&realtime, &input](const CaptureResult &result) {
                         if (input.isScreen()) realtime.acceptOneShot(result);
                     });
    translationWindow.show();
    GlobalShortcutManager shortcuts;
    OverlayInteractionController overlayInteraction(translationWindow, settings, shortcuts, {
        [&] { return captureCoordinator.isSelecting(); },
        [&] { if (input.isScreen()) captureCoordinator.beginSelection(); },
        [&] { return input.isRunning(); },
        [&] { input.start(); },
        [&] { input.stop(); },
        [&] { application.quit(); },
        [&] { return input.isScreen(); }
    });
    OverlayTrayController tray(overlayInteraction, translationWindow);
    QObject::connect(&input, &ProductionInputController::stateChanged, &overlayInteraction, &OverlayInteractionController::refreshState);
    QObject::connect(&settings, &SettingsManager::inputModeChanged, &overlayInteraction, &OverlayInteractionController::refreshState);
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &input, &ProductionInputController::stop);
    QObject::connect(&realtime, &RealtimePipelineCoordinator::runningChanged,
                     &overlayInteraction, &OverlayInteractionController::refreshState);
    QObject::connect(&captureCoordinator, &CaptureCoordinator::selectionStarted,
                     &overlayInteraction, &OverlayInteractionController::refreshState);
    QObject::connect(&captureCoordinator, &CaptureCoordinator::selectionFinished,
                     &overlayInteraction, &OverlayInteractionController::refreshState);
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &shortcuts,
                     &GlobalShortcutManager::unregisterAll);
    overlayInteraction.initialize();

    return application.exec();
}
