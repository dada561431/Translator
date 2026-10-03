#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>

#include "app/CaptureCoordinator.h"
#include "app/OcrCoordinator.h"
#include "app/TranslationCoordinator.h"
#include "config/SettingsManager.h"
#include "gui/TranslationWindow.h"
#include "ocr/TesseractOcrEngine.h"
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
    OcrCoordinator ocrCoordinator([] {
        return std::make_unique<TesseractOcrEngine>();
    });
    TranslationCoordinator translationCoordinator(settings, [&] {
        return TranslatorFactory::create(settings, *credentials);
    });
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
                     &ocrCoordinator, [&ocrCoordinator, &settings, &translationCoordinator](const CaptureResult &capture) {
                         translationCoordinator.invalidate();
                         ocrCoordinator.recognize(capture, settings.sourceLanguage());
                     });
    QObject::connect(&ocrCoordinator, &OcrCoordinator::resultReady,
                     &translationWindow, [&translationWindow, &settings, &translationCoordinator](const OcrResult &result) {
                         if (!result.sourceLanguage.isEmpty()
                             && result.sourceLanguage != settings.sourceLanguage()) return;
#ifndef NDEBUG
                         qDebug() << "[OCR] completed on GUI thread"
                                  << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
#endif
                         translationCoordinator.invalidate();
                         if (!result.isValid()) {
                             translationWindow.setOriginalText(QString());
                             translationWindow.setRegionFeedback(
                                 QStringLiteral("OCR failed: %1").arg(result.error));
                             return;
                         }
                         translationWindow.setOriginalText(result.text);
                         translationCoordinator.acceptOcr(result);
                     });
    translationWindow.show();

    return application.exec();
}
