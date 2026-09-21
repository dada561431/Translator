#include "gui/MainWindow.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("LunaTranslatorQt"));
    resize(800, 480);

    auto *centralWidget = new QWidget(this);
    auto *layout = new QVBoxLayout(centralWidget);
    auto *prototypeLabel = new QLabel(
        QStringLiteral("LunaTranslatorQt\nQt 6 migration prototype\nPhase 1"),
        centralWidget);

    prototypeLabel->setAlignment(Qt::AlignCenter);
    prototypeLabel->setWordWrap(true);
    layout->addWidget(prototypeLabel);

    setCentralWidget(centralWidget);
}
