#include <QApplication>

#include "gui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Translator"));
    QApplication::setOrganizationName(QStringLiteral("Translator"));

    MainWindow mainWindow;
    mainWindow.show();

    return application.exec();
}
