#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>

#include "app/CaptureCoordinator.h"
#include "app/OcrCoordinator.h"
#include "app/TranslationCoordinator.h"
#include "app/RealtimePipelineCoordinator.h"
#include "config/SettingsManager.h"
#include "gui/TranslationWindow.h"
#include "ocr/OcrEngineFactory.h"
#include "translator/TranslatorFactory.h"
#include "credentials/ICredentialStore.h"

#include <memory>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TranslatorProject"));
    QApplication::setApplicationName(QStringLiteral("Translator"));

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

    return application.exec();
}
