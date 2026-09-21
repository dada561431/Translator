#include "gui/TranslationWindow.h"

#include "config/SettingsManager.h"
#include "gui/SettingsDialog.h"

#include <QCloseEvent>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWindow>

TranslationWindow::TranslationWindow(SettingsManager &settings, QWidget *parent)
    : QWidget(parent,
              Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , settings_(settings)
{
    setObjectName(QStringLiteral("translationWindow"));
    setWindowTitle(QStringLiteral("Translator"));
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(520, 340);

    createUi();
    connectControls();
    setTranslationRunning(false);
    statusLabel_->setText(tr("Ready"));

    settingsDialog_ = new SettingsDialog(settings_, this);
    restoreWindowGeometry();
}

void TranslationWindow::setOriginalText(const QString &text)
{
    originalTextEdit_->setPlainText(text);
}

void TranslationWindow::setTranslatedText(const QString &text)
{
    translationTextEdit_->setPlainText(text);
}

bool TranslationWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == toolbar_ && event->type() == QEvent::MouseButtonPress) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && windowHandle()) {
            return windowHandle()->startSystemMove();
        }
    }

    return QWidget::eventFilter(watched, event);
}

void TranslationWindow::closeEvent(QCloseEvent *event)
{
    if (settingsDialog_) {
        settingsDialog_->close();
    }
    settings_.setWindowGeometry(saveGeometry());
    QWidget::closeEvent(event);
}

void TranslationWindow::createUi()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 8, 8, 8);

    auto *surface = new QWidget(this);
    surface->setObjectName(QStringLiteral("overlaySurface"));
    auto *surfaceLayout = new QVBoxLayout(surface);
    surfaceLayout->setContentsMargins(12, 10, 12, 10);
    surfaceLayout->setSpacing(8);
    outerLayout->addWidget(surface);

    toolbar_ = new QWidget(surface);
    toolbar_->setObjectName(QStringLiteral("toolbar"));
    toolbar_->installEventFilter(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar_);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(6);

    auto *titleLabel = new QLabel(tr("Translator"), toolbar_);
    titleLabel->setObjectName(QStringLiteral("windowTitleLabel"));
    titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    toolbarLayout->addWidget(titleLabel);
    toolbarLayout->addStretch();

    regionButton_ = new QPushButton(tr("Region"), toolbar_);
    regionButton_->setObjectName(QStringLiteral("regionButton"));
    startButton_ = new QPushButton(tr("Start"), toolbar_);
    startButton_->setObjectName(QStringLiteral("startButton"));
    stopButton_ = new QPushButton(tr("Stop"), toolbar_);
    stopButton_->setObjectName(QStringLiteral("stopButton"));
    settingsButton_ = new QPushButton(tr("Settings"), toolbar_);
    settingsButton_->setObjectName(QStringLiteral("settingsButton"));
    closeButton_ = new QPushButton(tr("Close"), toolbar_);
    closeButton_->setObjectName(QStringLiteral("closeButton"));

    toolbarLayout->addWidget(regionButton_);
    toolbarLayout->addWidget(startButton_);
    toolbarLayout->addWidget(stopButton_);
    toolbarLayout->addWidget(settingsButton_);
    toolbarLayout->addWidget(closeButton_);
    surfaceLayout->addWidget(toolbar_);

    auto *textSplitter = new QSplitter(Qt::Vertical, surface);
    textSplitter->setObjectName(QStringLiteral("textSplitter"));
    textSplitter->setChildrenCollapsible(false);

    auto *originalPanel = new QWidget(textSplitter);
    auto *originalLayout = new QVBoxLayout(originalPanel);
    originalLayout->setContentsMargins(0, 0, 0, 0);
    auto *originalLabel = new QLabel(tr("Original Text"), originalPanel);
    originalLabel->setObjectName(QStringLiteral("sectionLabel"));
    originalTextEdit_ = new QPlainTextEdit(originalPanel);
    originalTextEdit_->setObjectName(QStringLiteral("originalTextEdit"));
    originalTextEdit_->setReadOnly(true);
    originalLayout->addWidget(originalLabel);
    originalLayout->addWidget(originalTextEdit_);

    auto *translationPanel = new QWidget(textSplitter);
    auto *translationLayout = new QVBoxLayout(translationPanel);
    translationLayout->setContentsMargins(0, 0, 0, 0);
    auto *translationLabel = new QLabel(tr("Translation"), translationPanel);
    translationLabel->setObjectName(QStringLiteral("sectionLabel"));
    translationTextEdit_ = new QPlainTextEdit(translationPanel);
    translationTextEdit_->setObjectName(QStringLiteral("translationTextEdit"));
    translationTextEdit_->setReadOnly(true);
    translationLayout->addWidget(translationLabel);
    translationLayout->addWidget(translationTextEdit_);

    textSplitter->addWidget(originalPanel);
    textSplitter->addWidget(translationPanel);
    textSplitter->setStretchFactor(0, 1);
    textSplitter->setStretchFactor(1, 1);
    surfaceLayout->addWidget(textSplitter, 1);

    statusLabel_ = new QLabel(surface);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));
    statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    surfaceLayout->addWidget(statusLabel_);

    setStyleSheet(QStringLiteral(R"(
        #overlaySurface {
            background-color: rgba(31, 33, 36, 238);
            border: 1px solid #697078;
            border-radius: 8px;
        }
        #toolbar {
            border-bottom: 1px solid #555b62;
            padding-bottom: 6px;
        }
        #windowTitleLabel {
            color: #f4f5f2;
            font-weight: 600;
        }
        #sectionLabel {
            color: #d9ddd8;
            font-weight: 600;
        }
        #statusLabel {
            color: #b9c8c0;
        }
        QPushButton {
            color: #f4f5f2;
            background-color: #45494f;
            border: 1px solid #646a72;
            border-radius: 4px;
            padding: 5px 10px;
        }
        QPushButton:hover {
            background-color: #555b62;
        }
        QPushButton:disabled {
            color: #8f9499;
            background-color: #36393d;
            border-color: #474b50;
        }
        #startButton {
            background-color: #236f5a;
            border-color: #338c73;
        }
        QPlainTextEdit {
            color: #1d2022;
            background-color: rgba(248, 249, 247, 240);
            border: 1px solid #737980;
            border-radius: 4px;
            padding: 6px;
            selection-background-color: #2f806b;
        }
        QSplitter::handle {
            background-color: #555b62;
            height: 2px;
        }
    )"));
}

void TranslationWindow::connectControls()
{
    connect(regionButton_, &QPushButton::clicked, this, [this] {
        statusLabel_->setText(tr("Region selection is not implemented yet."));
    });
    connect(startButton_, &QPushButton::clicked, this, [this] {
        setTranslationRunning(true);
        statusLabel_->setText(tr("Translation UI started. Backend is not implemented yet."));
    });
    connect(stopButton_, &QPushButton::clicked, this, [this] {
        setTranslationRunning(false);
        statusLabel_->setText(tr("Stopped."));
    });
    connect(settingsButton_, &QPushButton::clicked, this, [this] {
        settingsDialog_->show();
        settingsDialog_->raise();
        settingsDialog_->activateWindow();
    });
    connect(closeButton_, &QPushButton::clicked, this, &QWidget::close);
}

void TranslationWindow::setTranslationRunning(bool running)
{
    startButton_->setEnabled(!running);
    stopButton_->setEnabled(running);
}

void TranslationWindow::restoreWindowGeometry()
{
    const QByteArray geometry = settings_.windowGeometry();
    if (!geometry.isEmpty() && restoreGeometry(geometry) && isVisibleOnAnyScreen()) {
        return;
    }

    resize(720, 460);
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        move(screen->availableGeometry().center() - rect().center());
    }
}

bool TranslationWindow::isVisibleOnAnyScreen() const
{
    const QRect windowRect = frameGeometry();
    for (const QScreen *screen : QGuiApplication::screens()) {
        if (screen->availableGeometry().intersects(windowRect)) {
            return true;
        }
    }
    return false;
}
