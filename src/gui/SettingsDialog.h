#pragma once

#include <QDialog>

class QComboBox;
class QShowEvent;
class SettingsManager;

class SettingsDialog final : public QDialog
{
public:
    explicit SettingsDialog(SettingsManager &settings, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void createUi();
    void loadSettings();
    void connectSettings();
    static void selectById(QComboBox *comboBox, const QString &id);

    SettingsManager &settings_;
    QComboBox *sourceLanguageCombo_ = nullptr;
    QComboBox *targetLanguageCombo_ = nullptr;
    QComboBox *ocrEngineCombo_ = nullptr;
    QComboBox *translatorCombo_ = nullptr;
};
