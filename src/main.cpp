#include <QApplication>
#include <QCoreApplication>

#include "app/CaptureCoordinator.h"
#include "app/OcrCoordinator.h"
#include "config/SettingsManager.h"
#include "gui/TranslationWindow.h"
#include "ocr/TesseractOcrEngine.h"

#include <memory>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TranslatorProject"));
    QApplication::setApplicationName(QStringLiteral("Translator"));

    SettingsManager settings;
    TranslationWindow translationWindow(settings);
    CaptureCoordinator captureCoordinator(translationWindow, settings);
    OcrCoordinator ocrCoordinator([] {
        return std::make_unique<TesseractOcrEngine>();
    });
    QObject::connect(&captureCoordinator, &CaptureCoordinator::captureCompleted,
                     &ocrCoordinator, [&ocrCoordinator, &settings](const CaptureResult &capture) {
                         ocrCoordinator.recognize(capture, settings.sourceLanguage());
                     });
    QObject::connect(&ocrCoordinator, &OcrCoordinator::resultReady,
                     &translationWindow, [&translationWindow](const OcrResult &result) {
                         if (!result.isValid()) {
                             translationWindow.setOriginalText(QString());
                             translationWindow.setRegionFeedback(
                                 QStringLiteral("OCR failed: %1").arg(result.error));
                             return;
                         }
                         translationWindow.setOriginalText(result.text);
                     });
    translationWindow.show();

    return application.exec();
}
