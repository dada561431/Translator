#include "capture/RegionSelector.h"

#include <QGuiApplication>
#include <QApplication>
#include <QCursor>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>

class RegionSelectionOverlay final : public QWidget
{
public:
    RegionSelectionOverlay(QScreen *screen, RegionSelector *selector)
        : QWidget(nullptr,
                  Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                      | Qt::NoDropShadowWindowHint)
        , screen_(screen)
        , selector_(selector)
    {
        setObjectName(QStringLiteral("regionSelectionOverlay"));
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_TranslucentBackground);
        setCursor(Qt::CrossCursor);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        winId();
        windowHandle()->setScreen(screen);
        setGeometry(screen->geometry());
    }

    void setSelection(const QRect &globalRect)
    {
        selection_ = globalRect.translated(-geometry().topLeft());
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0, 105));

        if (!selection_.isEmpty()) {
            painter.setCompositionMode(QPainter::CompositionMode_Clear);
            painter.fillRect(selection_, Qt::transparent);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.setPen(QPen(QColor(56, 210, 160), 2));
            painter.drawRect(selection_.adjusted(0, 0, -1, -1));
        }

        painter.setPen(Qt::white);
        QFont instructionFont = font();
        instructionFont.setPointSizeF(qMax(11.0, instructionFont.pointSizeF() + 1.0));
        instructionFont.setWeight(QFont::DemiBold);
        painter.setFont(instructionFont);
        painter.drawText(QRect(24, 20, qMax(0, width() - 48), 40),
                         Qt::AlignHCenter | Qt::AlignTop,
                         tr("Drag to select a capture region. Press Esc to cancel."));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && screen_) {
            setFocus(Qt::MouseFocusReason);
            selector_->beginDrag(screen_, event->globalPosition().toPoint());
            event->accept();
            return;
        }
        if (event->button() == Qt::RightButton) {
            selector_->cancelSelection();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        selector_->updateDrag(event->globalPosition().toPoint());
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            selector_->finishDrag(event->globalPosition().toPoint());
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            selector_->cancelSelection();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    QPointer<QScreen> screen_;
    RegionSelector *selector_ = nullptr;
    QRect selection_;
};

RegionSelector::RegionSelector(QObject *parent)
    : QObject(parent)
{
    connect(qGuiApp, &QGuiApplication::screenRemoved, this,
            [this](QScreen *) { cancelSelection(); });
}

RegionSelector::~RegionSelector()
{
    if (active_) {
        qApp->removeEventFilter(this);
    }
    for (const QPointer<QWidget> &overlay : overlays_) {
        delete overlay.data();
    }
}

bool RegionSelector::eventFilter(QObject *watched, QEvent *event)
{
    if (active_ && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        cancelSelection();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void RegionSelector::start()
{
    if (active_) {
        return;
    }

    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty()) {
        QTimer::singleShot(0, this, &RegionSelector::selectionCanceled);
        return;
    }

    active_ = true;
    dragging_ = false;
    activeScreen_.clear();
    qApp->installEventFilter(this);

    RegionSelectionOverlay *focusedOverlay = nullptr;
    for (QScreen *screen : screens) {
        auto *overlay = new RegionSelectionOverlay(screen, this);
        overlays_.append(overlay);
        overlay->show();
        overlay->raise();
        if (screen->geometry().contains(QCursor::pos())) {
            focusedOverlay = overlay;
        }
    }
    if (focusedOverlay) {
        focusedOverlay->activateWindow();
        focusedOverlay->setFocus(Qt::OtherFocusReason);
    }
}

bool RegionSelector::isActive() const
{
    return active_;
}

QRect RegionSelector::normalizedRegion(const QPoint &start, const QPoint &end)
{
    return QRect(QPoint(qMin(start.x(), end.x()), qMin(start.y(), end.y())),
                 QSize(qAbs(end.x() - start.x()), qAbs(end.y() - start.y())));
}

bool RegionSelector::isValidRegion(const QRect &region, int minimumSize)
{
    return region.isValid() && region.width() >= minimumSize
        && region.height() >= minimumSize;
}

void RegionSelector::beginDrag(QScreen *screen, const QPoint &globalPosition)
{
    if (!active_ || !screen) {
        return;
    }

    activeScreen_ = screen;
    dragStart_ = clampToActiveScreen(globalPosition);
    dragging_ = true;
    updateDrag(dragStart_);
}

void RegionSelector::updateDrag(const QPoint &globalPosition)
{
    if (!active_ || !dragging_ || !activeScreen_) {
        return;
    }

    const QRect region = normalizedRegion(dragStart_, clampToActiveScreen(globalPosition));
    for (const QPointer<QWidget> &overlayWidget : overlays_) {
        auto *overlay = static_cast<RegionSelectionOverlay *>(overlayWidget.data());
        if (overlay && overlay->geometry() == activeScreen_->geometry()) {
            overlay->setSelection(region);
        }
    }
}

void RegionSelector::finishDrag(const QPoint &globalPosition)
{
    if (!active_ || !dragging_ || !activeScreen_) {
        cancelSelection();
        return;
    }

    const QRect region = normalizedRegion(dragStart_, clampToActiveScreen(globalPosition));
    QPointer<QScreen> selectedScreen = activeScreen_;
    dragging_ = false;

    if (!isValidRegion(region)) {
        cancelSelection();
        return;
    }

    active_ = false;
    activeScreen_.clear();
    qApp->removeEventFilter(this);
    closeOverlays();
    QTimer::singleShot(0, this, [this, region, selectedScreen] {
        if (selectedScreen) {
            emit regionSelected(region, selectedScreen);
        } else {
            emit selectionCanceled();
        }
    });
}

void RegionSelector::cancelSelection()
{
    if (!active_) {
        return;
    }

    active_ = false;
    dragging_ = false;
    activeScreen_.clear();
    qApp->removeEventFilter(this);
    closeOverlays();
    QTimer::singleShot(0, this, &RegionSelector::selectionCanceled);
}

void RegionSelector::closeOverlays()
{
    for (const QPointer<QWidget> &overlay : overlays_) {
        if (overlay) {
            overlay->hide();
            overlay->deleteLater();
        }
    }
    overlays_.clear();
}

QPoint RegionSelector::clampToActiveScreen(const QPoint &globalPosition) const
{
    if (!activeScreen_) {
        return globalPosition;
    }

    const QRect geometry = activeScreen_->geometry();
    return QPoint(qBound(geometry.left(), globalPosition.x(), geometry.right()),
                  qBound(geometry.top(), globalPosition.y(), geometry.bottom()));
}
