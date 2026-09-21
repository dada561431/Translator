#include "gui/TranslationWindow.h"

#include "config/SettingsManager.h"
#include "gui/SettingsDialog.h"

#include <QCloseEvent>
#include <QColor>
#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace {

constexpr int kResizeMargin = 7;

QGraphicsDropShadowEffect *createSubtitleShadow(QObject *parent, int blurRadius)
{
    auto *shadow = new QGraphicsDropShadowEffect(parent);
    shadow->setBlurRadius(blurRadius);
    shadow->setColor(QColor(0, 0, 0, 235));
    shadow->setOffset(0, 2);
    return shadow;
}

} // namespace

TranslationWindow::TranslationWindow(SettingsManager &settings, QWidget *parent)
    : QWidget(parent,
              Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , settings_(settings)
{
    setObjectName(QStringLiteral("translationWindow"));
    setWindowTitle(QStringLiteral("Translator"));
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setMinimumSize(420, 120);

    createUi();
    connectControls();
    setTranslationRunning(false);
    translatedLabel_->setText(tr("实时翻译将在这里显示"));
    originalLabel_->setText(tr("Original text appears here"));

    settingsDialog_ = new SettingsDialog(settings_, this);
    restoreWindowGeometry();
    toolbar_->hide();
}

void TranslationWindow::setTranslatedText(const QString &text)
{
    if (translatedPlaceholder_ && text.isEmpty()) {
        return;
    }
    if (!text.isEmpty()) {
        translatedPlaceholder_ = false;
    }
    translatedLabel_->setText(text);
}

void TranslationWindow::setOriginalText(const QString &text)
{
    if (originalPlaceholder_ && text.isEmpty()) {
        return;
    }
    if (!text.isEmpty()) {
        originalPlaceholder_ = false;
    }
    originalLabel_->setText(text);
}

void TranslationWindow::setRegionFeedback(const QString &message)
{
    regionButton_->setToolTip(message);
    showToolbarStatus(message);
}

bool TranslationWindow::eventFilter(QObject *watched, QEvent *event)
{
    const auto *watchedWidget = qobject_cast<QWidget *>(watched);
    const bool isHoverWidget = watchedWidget
        && (watchedWidget == subtitleArea_ || watchedWidget == toolbar_
            || toolbar_->isAncestorOf(watchedWidget));
    if (isHoverWidget && event->type() == QEvent::Enter) {
        toolbarHideTimer_->stop();
        toolbar_->show();
    } else if (isHoverWidget && event->type() == QEvent::Leave) {
        scheduleToolbarHide();
    }

    if (watched == toolbar_ && event->type() == QEvent::MouseButtonPress) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && windowHandle()) {
            return windowHandle()->startSystemMove();
        }
    }

    if (watched == subtitleArea_) {
        const auto mouseEventType = event->type();
        if (mouseEventType == QEvent::MouseMove) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            updateResizeCursor(resizeEdgesAt(subtitleArea_->mapTo(this,
                                                                  mouseEvent->position().toPoint())));
        } else if (mouseEventType == QEvent::MouseButtonPress) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && windowHandle()) {
                const Qt::Edges edges = resizeEdgesAt(
                    subtitleArea_->mapTo(this, mouseEvent->position().toPoint()));
                return edges ? windowHandle()->startSystemResize(edges)
                             : windowHandle()->startSystemMove();
            }
        } else if (mouseEventType == QEvent::Leave) {
            unsetCursor();
        }
    }

    return QWidget::eventFilter(watched, event);
}

void TranslationWindow::enterEvent(QEnterEvent *event)
{
    toolbarHideTimer_->stop();
    toolbar_->show();
    QWidget::enterEvent(event);
}

void TranslationWindow::leaveEvent(QEvent *event)
{
    scheduleToolbarHide();
    QWidget::leaveEvent(event);
}

void TranslationWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const int toolbarWidth = qMax(0, width() - 16);
    toolbar_->setGeometry(8, 6, toolbarWidth, toolbar_->sizeHint().height());
    toolbar_->raise();
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
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(5);

    toolbar_ = new QWidget(this);
    toolbar_->setObjectName(QStringLiteral("toolbar"));
    toolbar_->installEventFilter(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar_);
    toolbarLayout->setContentsMargins(8, 5, 8, 5);
    toolbarLayout->setSpacing(6);

    auto *titleLabel = new QLabel(tr("Translator"), toolbar_);
    titleLabel->setObjectName(QStringLiteral("windowTitleLabel"));
    titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    titleLabel->installEventFilter(this);
    toolbarLayout->addWidget(titleLabel);

    statusLabel_ = new QLabel(toolbar_);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));
    statusLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    statusLabel_->setMinimumWidth(0);
    statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusLabel_->installEventFilter(this);
    statusLabel_->hide();
    toolbarLayout->addWidget(statusLabel_, 1);

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

    const QList<QPushButton *> toolbarButtons = {
        regionButton_, startButton_, stopButton_, settingsButton_, closeButton_};
    for (QPushButton *button : toolbarButtons) {
        button->installEventFilter(this);
    }

    toolbarLayout->addWidget(regionButton_);
    toolbarLayout->addWidget(startButton_);
    toolbarLayout->addWidget(stopButton_);
    toolbarLayout->addWidget(settingsButton_);
    toolbarLayout->addWidget(closeButton_);
    subtitleArea_ = new QWidget(this);
    subtitleArea_->setObjectName(QStringLiteral("subtitleArea"));
    subtitleArea_->setMouseTracking(true);
    subtitleArea_->installEventFilter(this);
    auto *subtitleLayout = new QVBoxLayout(subtitleArea_);
    subtitleLayout->setContentsMargins(18, 8, 18, 10);
    subtitleLayout->setSpacing(5);

    translatedLabel_ = new QLabel(subtitleArea_);
    translatedLabel_->setObjectName(QStringLiteral("translatedLabel"));
    translatedLabel_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    translatedLabel_->setWordWrap(true);
    translatedLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    translatedLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QFont translatedFont = translatedLabel_->font();
    translatedFont.setPointSizeF(qMax(16.0, translatedFont.pointSizeF() + 6.0));
    translatedFont.setWeight(QFont::DemiBold);
    translatedLabel_->setFont(translatedFont);
    translatedLabel_->setGraphicsEffect(createSubtitleShadow(translatedLabel_, 7));

    originalLabel_ = new QLabel(subtitleArea_);
    originalLabel_->setObjectName(QStringLiteral("originalLabel"));
    originalLabel_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    originalLabel_->setWordWrap(true);
    originalLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    originalLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QFont originalFont = originalLabel_->font();
    originalFont.setPointSizeF(qMax(12.0, originalFont.pointSizeF() + 2.0));
    originalLabel_->setFont(originalFont);
    originalLabel_->setGraphicsEffect(createSubtitleShadow(originalLabel_, 6));

    subtitleLayout->addStretch();
    subtitleLayout->addWidget(translatedLabel_);
    subtitleLayout->addWidget(originalLabel_);
    subtitleLayout->addStretch();
    layout->addWidget(subtitleArea_, 1);

    setStyleSheet(QStringLiteral(R"(
        #translationWindow, #subtitleArea {
            background: transparent;
        }
        #toolbar {
            background-color: rgba(28, 30, 33, 215);
            border: 1px solid rgba(120, 126, 132, 190);
            border-radius: 6px;
        }
        #windowTitleLabel, #statusLabel {
            color: #f4f5f2;
        }
        #statusLabel {
            color: #c5d2cc;
        }
        #translatedLabel {
            color: white;
            background: transparent;
        }
        #originalLabel {
            color: #e1e5e2;
            background: transparent;
        }
        QPushButton {
            color: #f4f5f2;
            background-color: rgba(69, 73, 79, 225);
            border: 1px solid #646a72;
            border-radius: 4px;
            padding: 5px 10px;
        }
        QPushButton:hover {
            background-color: rgba(85, 91, 98, 240);
        }
        QPushButton:disabled {
            color: #8f9499;
            background-color: rgba(54, 57, 61, 210);
            border-color: #474b50;
        }
        #startButton {
            background-color: rgba(35, 111, 90, 230);
            border-color: #338c73;
        }
    )"));
}

void TranslationWindow::connectControls()
{
    toolbarHideTimer_ = new QTimer(this);
    toolbarHideTimer_->setSingleShot(true);
    toolbarHideTimer_->setInterval(400);
    connect(toolbarHideTimer_, &QTimer::timeout, this, [this] {
        if (!rect().contains(mapFromGlobal(QCursor::pos()))) {
            toolbar_->hide();
        }
    });

    statusClearTimer_ = new QTimer(this);
    statusClearTimer_->setSingleShot(true);
    statusClearTimer_->setInterval(3000);
    connect(statusClearTimer_, &QTimer::timeout, statusLabel_, &QWidget::hide);

    connect(regionButton_, &QPushButton::clicked,
            this, &TranslationWindow::regionSelectionRequested);
    connect(startButton_, &QPushButton::clicked, this, [this] {
        setTranslationRunning(true);
        showToolbarStatus(tr("Translation UI started. Backend is not implemented yet."));
    });
    connect(stopButton_, &QPushButton::clicked, this, [this] {
        setTranslationRunning(false);
        showToolbarStatus(tr("Stopped."));
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

void TranslationWindow::showToolbarStatus(const QString &message)
{
    statusLabel_->setText(message);
    statusLabel_->setToolTip(message);
    statusLabel_->show();
    statusClearTimer_->start();
}

void TranslationWindow::scheduleToolbarHide()
{
    toolbarHideTimer_->start();
}

Qt::Edges TranslationWindow::resizeEdgesAt(const QPoint &position) const
{
    Qt::Edges edges;
    if (position.x() <= kResizeMargin) {
        edges |= Qt::LeftEdge;
    } else if (position.x() >= width() - kResizeMargin) {
        edges |= Qt::RightEdge;
    }
    if (position.y() <= kResizeMargin) {
        edges |= Qt::TopEdge;
    } else if (position.y() >= height() - kResizeMargin) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

void TranslationWindow::updateResizeCursor(Qt::Edges edges)
{
    if (edges == (Qt::LeftEdge | Qt::TopEdge)
        || edges == (Qt::RightEdge | Qt::BottomEdge)) {
        setCursor(Qt::SizeFDiagCursor);
    } else if (edges == (Qt::RightEdge | Qt::TopEdge)
               || edges == (Qt::LeftEdge | Qt::BottomEdge)) {
        setCursor(Qt::SizeBDiagCursor);
    } else if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge)) {
        setCursor(Qt::SizeHorCursor);
    } else if (edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge)) {
        setCursor(Qt::SizeVerCursor);
    } else {
        unsetCursor();
    }
}

void TranslationWindow::restoreWindowGeometry()
{
    const QByteArray geometry = settings_.windowGeometry();
    if (!geometry.isEmpty() && restoreGeometry(geometry) && isVisibleOnAnyScreen()) {
        return;
    }

    resize(760, 190);
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
