#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QVector>

class QScreen;
class QWidget;
class RegionSelectionOverlay;

class RegionSelector final : public QObject
{
    Q_OBJECT

public:
    explicit RegionSelector(QObject *parent = nullptr);
    ~RegionSelector() override;

    void start();
    bool isActive() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

public:
    static QRect normalizedRegion(const QPoint &start, const QPoint &end);
    static bool isValidRegion(const QRect &region, int minimumSize = 10);

signals:
    void regionSelected(const QRect &globalLogicalRect, QScreen *screen);
    void selectionCanceled();

private:
    friend class RegionSelectionOverlay;

    void beginDrag(QScreen *screen, const QPoint &globalPosition);
    void updateDrag(const QPoint &globalPosition);
    void finishDrag(const QPoint &globalPosition);
    void cancelSelection();
    void closeOverlays();
    QPoint clampToActiveScreen(const QPoint &globalPosition) const;

    QVector<QPointer<QWidget>> overlays_;
    QPointer<QScreen> activeScreen_;
    QPoint dragStart_;
    bool active_ = false;
    bool dragging_ = false;
};
