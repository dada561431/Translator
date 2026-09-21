#include <QApplication>
#include <QCoreApplication>

#include "app/CaptureCoordinator.h"
#include "config/SettingsManager.h"
#include "gui/TranslationWindow.h"

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TranslatorProject"));
    QApplication::setApplicationName(QStringLiteral("Translator"));

    SettingsManager settings;
    TranslationWindow translationWindow(settings);
    CaptureCoordinator captureCoordinator(translationWindow, settings);
    translationWindow.show();

    return application.exec();
}
