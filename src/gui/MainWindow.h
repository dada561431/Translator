#pragma once

#include <QMainWindow>

class QComboBox;
class QPlainTextEdit;
class QPushButton;
class SettingsManager;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(SettingsManager &settings, QWidget *parent = nullptr);

private:
    void createUi();
    void loadSettings();
    void connectSettings();
    void setTranslationRunning(bool running);
    static void selectById(QComboBox *comboBox, const QString &id);

    SettingsManager &settings_;
    QComboBox *sourceLanguageCombo_ = nullptr;
    QComboBox *targetLanguageCombo_ = nullptr;
    QComboBox *ocrEngineCombo_ = nullptr;
    QComboBox *translatorCombo_ = nullptr;
    QPushButton *selectRegionButton_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QPlainTextEdit *originalTextEdit_ = nullptr;
    QPlainTextEdit *translationTextEdit_ = nullptr;
};
