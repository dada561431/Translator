#include "gui/TranslationWindow.h"

#include "config/SettingsManager.h"
#include "gui/SettingsDialog.h"

#include <QCloseEvent>
#include <QComboBox>
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
#include <QShowEvent>
#include <QHideEvent>
#include <QSignalBlocker>
#include <QDebug>
#include <utility>

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

TranslationWindow::TranslationWindow(SettingsManager &settings, QWidget *parent, ICredentialStore *credentials,
                                     WindowCaptureExclusion::Backend captureBackend, GeometryBackend geometryBackend)
    : QWidget(parent,
              Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , settings_(settings)
    , captureExclusion_(std::move(captureBackend))
    , geometryBackend_(std::move(geometryBackend))
{
    if (!geometryBackend_.move) geometryBackend_.move = [this] { return windowHandle()->startSystemMove(); };
    if (!geometryBackend_.resize) geometryBackend_.resize = [this](Qt::Edges edges) { return windowHandle()->startSystemResize(edges); };
    setObjectName(QStringLiteral("translationWindow"));
    setWindowTitle(QStringLiteral("Translator"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    setMinimumSize(420, 120);

    createUi();
    connectControls();
    setTranslationRunning(false);
    translatedLabel_->setText(tr("实时翻译将在这里显示"));
    originalLabel_->setText(tr("Original text appears here"));

    settingsDialog_ = new SettingsDialog(settings_, this, credentials);
    connect(&settings_, &SettingsManager::overlayAppearanceChanged, this, &TranslationWindow::applyAppearance);
    connect(&settings_, &SettingsManager::overlayCaptureExclusionChanged, this, &TranslationWindow::applyCaptureExclusion);
    connect(&settings_, &SettingsManager::overlayDragLockedChanged, this, [this] { setDragLocked(settings_.overlayDragLocked()); });
    setDragLocked(settings_.overlayDragLocked());
    applyAppearance();
    restoreWindowGeometry();
}

void TranslationWindow::applyAppearance()
{
    const auto appearance = settings_.overlayAppearance();
    QFont translated = translatedLabel_->font(), original = originalLabel_->font();
    translated.setPointSizeF(appearance.translationFontSize);
    original.setPointSizeF(appearance.originalFontSize);
    translatedLabel_->setFont(translated);
    originalLabel_->setFont(original);
    translatedLabel_->setVisible(appearance.showTranslation);
    originalLabel_->setVisible(appearance.showOriginal);
    subtitleArea_->setStyleSheet(QStringLiteral("#subtitleArea { background-color: rgba(28,30,33,%1); }")
                                    .arg(qRound(255.0 * appearance.backgroundOpacity / 100.0)));
    const int contentHeight = (appearance.showTranslation ? translatedLabel_->fontMetrics().height() : 0)
        + (appearance.showOriginal ? originalLabel_->fontMetrics().height() : 0)
        + (appearance.showOriginal && appearance.showTranslation ? 5 : 0) + 30;
    setMinimumSize(qMax(420, toolbar_->minimumSizeHint().width() + 16), qMax(120, contentHeight));
    updateGeometry();
}

void TranslationWindow::applyCaptureExclusion()
{
    const auto previous = captureExclusion_.status();
    captureExclusion_.setExcluded(this, settings_.overlayExcludeFromCapture());
    const auto status = captureExclusion_.status();
    const bool unavailable = status == WindowCaptureExclusion::Status::Unsupported
        || status == WindowCaptureExclusion::Status::Failed;
    if (settingsDialog_) settingsDialog_->setCaptureExclusionAvailable(!unavailable && captureExclusion_.supported());
    if (unavailable && status != previous) {
        interactionFeedback_->setText(tr("Capture exclusion unavailable"));
        interactionFeedback_->adjustSize();
        interactionFeedback_->move((width() - interactionFeedback_->width()) / 2,
                                   toolbar_->geometry().bottom() + 4);
        interactionFeedback_->show();
        interactionFeedback_->raise();
        interactionFeedbackTimer_->start();
        qWarning().noquote() << "[CaptureExclusion]" << captureExclusion_.lastError();
    }
}

bool TranslationWindow::event(QEvent *event)
{
    const bool handled = QWidget::event(event);
    if (event->type() == QEvent::WinIdChange && settingsDialog_) applyCaptureExclusion();
    return handled;
}

void TranslationWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    applyCaptureExclusion();
    emit overlayVisibilityChanged();
}

void TranslationWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    emit overlayVisibilityChanged();
}

void TranslationWindow::setDragLocked(bool locked)
{
    const bool changed = dragLocked_ != locked;
    dragLocked_ = locked;
    unsetCursor();
    const QSignalBlocker blocker(lockButton_);
    lockButton_->setChecked(locked);
    lockButton_->setText(locked ? tr("Unlock") : tr("Lock"));
    lockButton_->setToolTip(locked ? tr("Unlock overlay position") : tr("Lock overlay position"));
    if (changed) emit dragLockedChanged();
}

void TranslationWindow::setInteractionMode(OverlayInteractionMode mode)
{
    if (interactionMode_ == mode) return;
    const QRect previousGeometry = geometry();
    const bool wasVisible = isVisible();
    const bool settingsVisible = settingsDialog_ && settingsDialog_->isVisible();
    QScreen *previousScreen = screen();
    interactionMode_ = mode;
    toolbarHideTimer_->stop();
    unsetCursor();
    const bool clickThrough = mode == OverlayInteractionMode::ClickThrough;
    Qt::WindowFlags flags = windowFlags();
    flags.setFlag(Qt::WindowTransparentForInput, clickThrough);
    flags.setFlag(Qt::WindowDoesNotAcceptFocus, clickThrough);
    // QWidget flag changes hide the window and may recreate its native handle.
    setWindowFlags(flags);
    if (windowHandle() && previousScreen) windowHandle()->setScreen(previousScreen);
    setGeometry(previousGeometry);
    toolbar_->setVisible(!clickThrough);
    if (wasVisible) show();
    applyCaptureExclusion();
    if (settingsVisible) settingsDialog_->show();
    if (!clickThrough) {
        toolbarHideTimer_->start(1000);
    }
    interactionFeedback_->setText(clickThrough ? tr("Mouse passthrough ON") : tr("Interactive mode"));
    interactionFeedback_->adjustSize();
    interactionFeedback_->move((width() - interactionFeedback_->width()) / 2,
                               toolbar_->geometry().bottom() + 4);
    interactionFeedback_->show();
    interactionFeedback_->raise();
    interactionFeedbackTimer_->start();
    emit interactionModeChanged();
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
    regionButton_->setToolTip(settings_.inputMode() == QLatin1String("screen") ? message
        : tr("Region is only available in Screen mode."));
    inputModeCombo_->setToolTip(message);
    showToolbarStatus(message);
}

void TranslationWindow::setTranslationState(TranslationState state)
{
    if (state == TranslationState::Idle) {
        translatedPlaceholder_ = true;
        translatedLabel_->setText(tr("实时翻译将在这里显示"));
    } else if (state == TranslationState::Pending) {
        translatedPlaceholder_ = false;
        translatedLabel_->setText(tr("翻译中…"));
    } else if (state == TranslationState::Error) {
        translatedPlaceholder_ = false;
        translatedLabel_->setText(tr("翻译失败"));
    }
}

bool TranslationWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (interactionMode_ == OverlayInteractionMode::ClickThrough)
        return QWidget::eventFilter(watched, event);
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

    if (!geometryInteractionAllowed()) return QWidget::eventFilter(watched, event);
    if (watched == toolbar_ && event->type() == QEvent::MouseButtonPress) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && windowHandle()) {
            return geometryBackend_.move();
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
                return edges ? geometryBackend_.resize(edges) : geometryBackend_.move();
            }
        } else if (mouseEventType == QEvent::Leave) {
            unsetCursor();
        }
    }

    return QWidget::eventFilter(watched, event);
}

void TranslationWindow::enterEvent(QEnterEvent *event)
{
    if (interactionMode_ == OverlayInteractionMode::Interactive) {
        toolbarHideTimer_->stop();
        toolbar_->show();
    }
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
    if (interactionFeedback_) {
        interactionFeedback_->move((width() - interactionFeedback_->width()) / 2,
                                   toolbar_->geometry().bottom() + 4);
        interactionFeedback_->raise();
    }
}

void TranslationWindow::closeEvent(QCloseEvent *event)
{
    emit stopRequested();
    if (settingsDialog_) {
        settingsDialog_->close();
    }
    settings_.setWindowGeometry(saveGeometry());
    QWidget::closeEvent(event);
    emit closed();
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
    titleLabel->hide();
    inputModeCombo_ = new QComboBox(toolbar_);
    inputModeCombo_->setObjectName(QStringLiteral("inputModeCombo"));
    inputModeCombo_->addItem(tr("Screen"), "screen");
    inputModeCombo_->addItem(tr("Microphone"), "microphone");
    inputModeCombo_->addItem(tr("System Audio"), "system-audio");
    inputModeCombo_->setFixedWidth(inputModeCombo_->fontMetrics().horizontalAdvance(tr("System Audio")) + 28);
    toolbarLayout->addWidget(inputModeCombo_);

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
    lockButton_ = new QPushButton(tr("Lock"), toolbar_);
    lockButton_->setObjectName(QStringLiteral("lockButton"));
    lockButton_->setCheckable(true);
    lockButton_->setFixedWidth(lockButton_->fontMetrics().horizontalAdvance(tr("Unlock")) + 24);

    const QList<QPushButton *> toolbarButtons = {
        regionButton_, startButton_, stopButton_, settingsButton_, lockButton_, closeButton_};
    for (QPushButton *button : toolbarButtons) {
        button->installEventFilter(this);
    }

    toolbarLayout->addWidget(regionButton_);
    toolbarLayout->addWidget(startButton_);
    toolbarLayout->addWidget(stopButton_);
    toolbarLayout->addWidget(settingsButton_);
    toolbarLayout->addWidget(lockButton_);
    toolbarLayout->addWidget(closeButton_);
    auto refreshMode = [this] {
        const QSignalBlocker blocker(inputModeCombo_);
        inputModeCombo_->setCurrentIndex(inputModeCombo_->findData(settings_.inputMode()));
        const bool screen = settings_.inputMode() == QLatin1String("screen");
        regionButton_->setEnabled(screen);
        regionButton_->setToolTip(screen ? tr("Screen Region") : tr("Region is only available in Screen mode."));
    };
    connect(&settings_, &SettingsManager::inputModeChanged, this, refreshMode);
    connect(inputModeCombo_, &QComboBox::currentIndexChanged, this, [this] {
        settings_.setInputMode(inputModeCombo_->currentData().toString());
    });
    refreshMode();
    subtitleArea_ = new QWidget(this);
    subtitleArea_->setObjectName(QStringLiteral("subtitleArea"));
    subtitleArea_->setMouseTracking(true);
    subtitleArea_->installEventFilter(this);
    auto *subtitleLayout = new QVBoxLayout(subtitleArea_);
    subtitleLayout->setContentsMargins(18, 8, 18, 10);
    subtitleLayout->setSpacing(5);

    translatedLabel_ = new QLabel(subtitleArea_);
    translatedLabel_->setObjectName(QStringLiteral("translatedLabel"));
    translatedLabel_->setTextFormat(Qt::PlainText);
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
    originalLabel_->setTextFormat(Qt::PlainText);
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

    interactionFeedback_ = new QLabel(this);
    interactionFeedback_->setObjectName(QStringLiteral("interactionFeedback"));
    interactionFeedback_->setTextFormat(Qt::PlainText);
    interactionFeedback_->setAttribute(Qt::WA_TransparentForMouseEvents);
    interactionFeedback_->setStyleSheet(QStringLiteral("color: white; background: rgba(28,30,33,210); padding: 3px 8px;"));
    interactionFeedback_->hide();

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
            padding: 5px 4px;
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
        if (interactionMode_ == OverlayInteractionMode::ClickThrough
            || !rect().contains(mapFromGlobal(QCursor::pos()))) {
            toolbar_->hide();
        }
    });

    statusClearTimer_ = new QTimer(this);
    statusClearTimer_->setSingleShot(true);
    statusClearTimer_->setInterval(3000);
    connect(statusClearTimer_, &QTimer::timeout, statusLabel_, &QWidget::hide);
    interactionFeedbackTimer_ = new QTimer(this);
    interactionFeedbackTimer_->setSingleShot(true);
    interactionFeedbackTimer_->setInterval(1000);
    connect(interactionFeedbackTimer_, &QTimer::timeout, interactionFeedback_, &QWidget::hide);

    connect(regionButton_, &QPushButton::clicked, this, [this] { requestControl(OverlayControlAction::SelectRegion); });
    connect(startButton_, &QPushButton::clicked, this, [this] { requestControl(OverlayControlAction::Start); });
    connect(stopButton_, &QPushButton::clicked, this, [this] { requestControl(OverlayControlAction::Stop); });
    connect(settingsButton_, &QPushButton::clicked, this, [this] { requestControl(OverlayControlAction::OpenSettings); });
    connect(lockButton_, &QPushButton::clicked, this, [this] { requestControl(OverlayControlAction::ToggleLock); });
    connect(closeButton_, &QPushButton::clicked, this, [this] { requestControl(OverlayControlAction::Exit); });
}

void TranslationWindow::openSettings()
{
    settingsDialog_->show();
    settingsDialog_->raise();
    settingsDialog_->activateWindow();
}

void TranslationWindow::requestControl(OverlayControlAction action)
{
    if (controlRouter_) { controlRouter_(action); return; }
    // Standalone widgets/probes retain their original signal contract.
    switch (action) {
    case OverlayControlAction::SelectRegion: emit regionSelectionRequested(); break;
    case OverlayControlAction::Start: emit startRequested(); break;
    case OverlayControlAction::Stop: emit stopRequested(); break;
    case OverlayControlAction::OpenSettings: openSettings(); break;
    case OverlayControlAction::ToggleLock: settings_.setOverlayDragLocked(!dragLocked_); break;
    case OverlayControlAction::Exit: close(); break;
    default: break;
    }
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
    if (interactionMode_ == OverlayInteractionMode::Interactive) toolbarHideTimer_->start(400);
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
