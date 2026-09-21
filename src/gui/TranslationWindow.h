#pragma once

#include <QWidget>

class QCloseEvent;
class QEnterEvent;
class QEvent;
class QLabel;
class QPoint;
class QPushButton;
class QResizeEvent;
class QTimer;
class SettingsDialog;
class SettingsManager;

class TranslationWindow final : public QWidget
{
public:
    explicit TranslationWindow(SettingsManager &settings, QWidget *parent = nullptr);

    void setTranslatedText(const QString &text);
    void setOriginalText(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void createUi();
    void connectControls();
    void setTranslationRunning(bool running);
    void showToolbarStatus(const QString &message);
    void scheduleToolbarHide();
    Qt::Edges resizeEdgesAt(const QPoint &position) const;
    void updateResizeCursor(Qt::Edges edges);
    void restoreWindowGeometry();
    bool isVisibleOnAnyScreen() const;

    SettingsManager &settings_;
    QWidget *toolbar_ = nullptr;
    QWidget *subtitleArea_ = nullptr;
    QPushButton *regionButton_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QPushButton *settingsButton_ = nullptr;
    QPushButton *closeButton_ = nullptr;
    QLabel *translatedLabel_ = nullptr;
    QLabel *originalLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QTimer *toolbarHideTimer_ = nullptr;
    QTimer *statusClearTimer_ = nullptr;
    SettingsDialog *settingsDialog_ = nullptr;
};
