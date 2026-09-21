#include <QApplication>

#include "gui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LunaTranslatorQt"));
    QApplication::setOrganizationName(QStringLiteral("LunaTranslatorQt"));

    MainWindow mainWindow;
    mainWindow.show();

    return application.exec();
}
