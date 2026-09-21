#include <QApplication>
#include <QCoreApplication>

#include "config/SettingsManager.h"
#include "gui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TranslatorProject"));
    QApplication::setApplicationName(QStringLiteral("Translator"));

    SettingsManager settings;
    MainWindow mainWindow(settings);
    mainWindow.show();

    return application.exec();
}
