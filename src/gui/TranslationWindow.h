#pragma once

#include <QWidget>
#include "translator/TranslationTypes.h"
#include "platform/WindowCaptureExclusion.h"
#include "gui/OverlayControlAction.h"
#include <functional>
#include <utility>

class QCloseEvent;
class QEnterEvent;
class QEvent;
class QLabel;
class QPoint;
class QPushButton;
class QResizeEvent;
class QTimer;
class QShowEvent;
class SettingsDialog;
class SettingsManager;
class ICredentialStore;

enum class OverlayInteractionMode { Interactive, ClickThrough };

class TranslationWindow final : public QWidget
{
    Q_OBJECT

public:
    struct GeometryBackend {
        std::function<bool()> move;
        std::function<bool(Qt::Edges)> resize;
    };
    explicit TranslationWindow(SettingsManager &settings, QWidget *parent = nullptr,
                               ICredentialStore *credentials = nullptr,
                               WindowCaptureExclusion::Backend captureBackend = {}, GeometryBackend geometryBackend = {});

    void setTranslatedText(const QString &text);
    void setOriginalText(const QString &text);
    void setRegionFeedback(const QString &message);
    void setTranslationState(TranslationState state);
    void setTranslationRunning(bool running);
    void setInteractionMode(OverlayInteractionMode mode);
    void setDragLocked(bool locked);
    bool dragLocked() const { return dragLocked_; }
    bool geometryInteractionAllowed() const { return !dragLocked_ && interactionMode_ == OverlayInteractionMode::Interactive; }
    void openSettings();
    SettingsDialog *settingsDialog() const { return settingsDialog_; }
    void setControlRouter(std::function<void(OverlayControlAction)> router) { controlRouter_ = std::move(router); }
    OverlayInteractionMode interactionMode() const { return interactionMode_; }
    WindowCaptureExclusion::Status captureExclusionStatus() const { return captureExclusion_.status(); }

signals:
    void regionSelectionRequested();
    void startRequested();
    void stopRequested();
    void interactionModeChanged();
    void dragLockedChanged();
    void overlayVisibilityChanged();
    void closed();

protected:
    bool event(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void createUi();
    void connectControls();
    void requestControl(OverlayControlAction action);
    void showToolbarStatus(const QString &message);
    void scheduleToolbarHide();
    Qt::Edges resizeEdgesAt(const QPoint &position) const;
    void updateResizeCursor(Qt::Edges edges);
    void restoreWindowGeometry();
    bool isVisibleOnAnyScreen() const;
    void applyAppearance();
    void applyCaptureExclusion();

    SettingsManager &settings_;
    WindowCaptureExclusion captureExclusion_;
    QWidget *toolbar_ = nullptr;
    QWidget *subtitleArea_ = nullptr;
    QPushButton *regionButton_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QPushButton *settingsButton_ = nullptr;
    QPushButton *closeButton_ = nullptr;
    QPushButton *lockButton_ = nullptr;
    QLabel *translatedLabel_ = nullptr;
    QLabel *originalLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *interactionFeedback_ = nullptr;
    QTimer *interactionFeedbackTimer_ = nullptr;
    QTimer *toolbarHideTimer_ = nullptr;
    QTimer *statusClearTimer_ = nullptr;
    SettingsDialog *settingsDialog_ = nullptr;
    bool translatedPlaceholder_ = true;
    bool originalPlaceholder_ = true;
    bool dragLocked_ = false;
    std::function<void(OverlayControlAction)> controlRouter_;
    GeometryBackend geometryBackend_;
    OverlayInteractionMode interactionMode_ = OverlayInteractionMode::Interactive;
};
