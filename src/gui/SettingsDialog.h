#pragma once

#include <QDialog>
#include <QHash>
#include <memory>
#include "credentials/ICredentialStore.h"

class QComboBox;
class QShowEvent;
class SettingsManager;
class QLabel;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QFormLayout;

class SettingsDialog final : public QDialog
{
public:
    explicit SettingsDialog(SettingsManager &settings, QWidget *parent = nullptr,
                            ICredentialStore *credentials = nullptr);
    void reject() override;

protected:
    void showEvent(QShowEvent *event) override;

private:
    void createUi();
    void loadSettings();
    void connectSettings();
    static void selectById(QComboBox *comboBox, const QString &id);
    void updateProvider();
    void updateCredentialStatus();
    bool applyCredentials();
    void clearDrafts();
    struct CredentialDraft { QString replacement; bool remove = false; bool editing = false; };
    std::unique_ptr<ICredentialStore> ownedCredentials_;
    ICredentialStore *credentials_ = nullptr;
    QHash<QString, CredentialDraft> drafts_;
    QString displayedProvider_;

    SettingsManager &settings_;
    QComboBox *sourceLanguageCombo_ = nullptr;
    QComboBox *targetLanguageCombo_ = nullptr;
    QComboBox *ocrEngineCombo_ = nullptr;
    QComboBox *translatorCombo_ = nullptr;
    QComboBox *planCombo_ = nullptr;
    QLineEdit *baseUrlEdit_ = nullptr;
    QLineEdit *modelEdit_ = nullptr;
    QLineEdit *apiKeyEdit_ = nullptr;
    QCheckBox *showKey_ = nullptr;
    QLabel *credentialStatus_ = nullptr;
    QLabel *errorLabel_ = nullptr;
    QLabel *privacyLabel_ = nullptr;
    QPushButton *replaceKey_ = nullptr;
    QPushButton *removeKey_ = nullptr;
    QWidget *credentialActions_ = nullptr;
    QWidget *keyEditor_ = nullptr;
    QFormLayout *form_ = nullptr;
};
