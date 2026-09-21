#pragma once

#include <QWidget>

class QCloseEvent;
class QEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class SettingsDialog;
class SettingsManager;

class TranslationWindow final : public QWidget
{
public:
    explicit TranslationWindow(SettingsManager &settings, QWidget *parent = nullptr);

    void setOriginalText(const QString &text);
    void setTranslatedText(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void createUi();
    void connectControls();
    void setTranslationRunning(bool running);
    void restoreWindowGeometry();
    bool isVisibleOnAnyScreen() const;

    SettingsManager &settings_;
    QWidget *toolbar_ = nullptr;
    QPushButton *regionButton_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QPushButton *settingsButton_ = nullptr;
    QPushButton *closeButton_ = nullptr;
    QPlainTextEdit *originalTextEdit_ = nullptr;
    QPlainTextEdit *translationTextEdit_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    SettingsDialog *settingsDialog_ = nullptr;
};
