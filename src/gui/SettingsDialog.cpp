#include "gui/SettingsDialog.h"
#include "config/SettingsManager.h"
#include "translator/TranslationProviderRegistry.h"
#include "translator/OpenAiCompatibleTranslator.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(SettingsManager &settings, QWidget *parent, ICredentialStore *credentials)
    : QDialog(parent), settings_(settings)
{
    if (!credentials) ownedCredentials_ = createPlatformCredentialStore();
    credentials_ = credentials ? credentials : ownedCredentials_.get();
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("Translator Settings"));
    setModal(false);
    setMinimumWidth(460);
    createUi();
    loadSettings();
    connectSettings();
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    clearDrafts();
    loadSettings();
    QDialog::showEvent(event);
}

void SettingsDialog::createUi()
{
    auto *layout = new QVBoxLayout(this);
    form_ = new QFormLayout();
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    sourceLanguageCombo_ = new QComboBox(this);
    sourceLanguageCombo_->setObjectName(QStringLiteral("sourceLanguageCombo"));
    sourceLanguageCombo_->addItem(tr("Auto (English OCR)"), QStringLiteral("auto"));
    targetLanguageCombo_ = new QComboBox(this);
    targetLanguageCombo_->setObjectName(QStringLiteral("targetLanguageCombo"));
    const QList<QPair<QString, QString>> languages = {
        {tr("Chinese"), QStringLiteral("zh")}, {tr("English"), QStringLiteral("en")},
        {tr("Japanese"), QStringLiteral("ja")}, {tr("Korean"), QStringLiteral("ko")}
    };
    for (const auto &language : languages) {
        sourceLanguageCombo_->addItem(language.first, language.second);
        targetLanguageCombo_->addItem(language.first, language.second);
    }
    ocrEngineCombo_ = new QComboBox(this);
    ocrEngineCombo_->setObjectName(QStringLiteral("ocrEngineCombo"));
    ocrEngineCombo_->addItem(tr("Tesseract"), QStringLiteral("tesseract"));
    translatorCombo_ = new QComboBox(this);
    translatorCombo_->setObjectName(QStringLiteral("translatorCombo"));
    for (const auto &info : TranslationProviderRegistry::providers())
        translatorCombo_->addItem(info.displayName, info.id);
    form_->addRow(tr("Source Language"), sourceLanguageCombo_);
    form_->addRow(tr("Target Language"), targetLanguageCombo_);
    form_->addRow(tr("OCR Engine"), ocrEngineCombo_);
    form_->addRow(tr("Translation Provider"), translatorCombo_);
    planCombo_ = new QComboBox(this);
    planCombo_->setObjectName(QStringLiteral("deepLPlanCombo"));
    planCombo_->addItem(tr("Free"), QStringLiteral("free"));
    planCombo_->addItem(tr("Pro"), QStringLiteral("pro"));
    form_->addRow(tr("Plan"), planCombo_);
    baseUrlEdit_ = new QLineEdit(this);
    baseUrlEdit_->setObjectName(QStringLiteral("baseUrlEdit"));
    baseUrlEdit_->setPlaceholderText(QStringLiteral("https://your-service.example/v1"));
    form_->addRow(tr("Base URL"), baseUrlEdit_);
    modelEdit_ = new QLineEdit(this);
    modelEdit_->setObjectName(QStringLiteral("modelEdit"));
    form_->addRow(tr("Model"), modelEdit_);
    credentialActions_ = new QWidget(this);
    auto *actions = new QHBoxLayout(credentialActions_);
    actions->setContentsMargins(0, 0, 0, 0);
    credentialStatus_ = new QLabel(credentialActions_);
    credentialStatus_->setObjectName(QStringLiteral("credentialStatus"));
    credentialStatus_->setWordWrap(true);
    replaceKey_ = new QPushButton(tr("Configure / Replace"), credentialActions_);
    replaceKey_->setObjectName(QStringLiteral("replaceKeyButton"));
    removeKey_ = new QPushButton(tr("Remove"), credentialActions_);
    removeKey_->setObjectName(QStringLiteral("removeKeyButton"));
    actions->addWidget(credentialStatus_, 1);
    actions->addWidget(replaceKey_);
    actions->addWidget(removeKey_);
    form_->addRow(tr("API Key"), credentialActions_);
    keyEditor_ = new QWidget(this);
    auto *editor = new QHBoxLayout(keyEditor_);
    editor->setContentsMargins(0, 0, 0, 0);
    apiKeyEdit_ = new QLineEdit(keyEditor_);
    apiKeyEdit_->setObjectName(QStringLiteral("apiKeyEdit"));
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    showKey_ = new QCheckBox(tr("Show"), keyEditor_);
    showKey_->setObjectName(QStringLiteral("showKeyCheck"));
    editor->addWidget(apiKeyEdit_, 1);
    editor->addWidget(showKey_);
    form_->addRow(tr("New API Key"), keyEditor_);
    layout->addLayout(form_);
    privacyLabel_ = new QLabel(tr("识别出的文字将发送至所选翻译服务。"), this);
    privacyLabel_->setWordWrap(true);
    layout->addWidget(privacyLabel_);
    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("credentialError"));
    errorLabel_->setWordWrap(true);
    layout->addWidget(errorLabel_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply
                                        | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("settingsButtonBox"));
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { applyCredentials(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (applyCredentials()) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    layout->addWidget(buttons);
}

void SettingsDialog::loadSettings()
{
    const QSignalBlocker source(sourceLanguageCombo_), target(targetLanguageCombo_), ocr(ocrEngineCombo_),
                         provider(translatorCombo_), plan(planCombo_), base(baseUrlEdit_), model(modelEdit_);
    selectById(sourceLanguageCombo_, settings_.sourceLanguage());
    selectById(targetLanguageCombo_, settings_.targetLanguage());
    selectById(ocrEngineCombo_, settings_.ocrEngine());
    selectById(translatorCombo_, settings_.translator());
    selectById(planCombo_, settings_.deepLEndpoint().contains(QStringLiteral("api.deepl.com"))
                           ? QStringLiteral("pro") : QStringLiteral("free"));
    baseUrlEdit_->setText(settings_.openAiBaseUrl());
    modelEdit_->setText(settings_.openAiModel());
    updateProvider();
}

void SettingsDialog::connectSettings()
{
    connect(sourceLanguageCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        settings_.setSourceLanguage(sourceLanguageCombo_->itemData(index).toString());
    });
    connect(targetLanguageCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        settings_.setTargetLanguage(targetLanguageCombo_->itemData(index).toString());
    });
    connect(ocrEngineCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        settings_.setOcrEngine(ocrEngineCombo_->itemData(index).toString());
    });
    connect(translatorCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        settings_.setTranslator(translatorCombo_->itemData(index).toString());
        updateProvider();
    });
    connect(planCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        settings_.setDeepLPlan(planCombo_->itemData(index).toString());
    });
    connect(replaceKey_, &QPushButton::clicked, this, [this] {
        auto &draft = drafts_[displayedProvider_];
        draft.editing = true;
        draft.remove = false;
        form_->setRowVisible(keyEditor_, true);
        apiKeyEdit_->setFocus();
        updateCredentialStatus();
    });
    connect(removeKey_, &QPushButton::clicked, this, [this] {
        drafts_[displayedProvider_] = {{}, true, false};
        updateProvider();
    });
    connect(apiKeyEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        drafts_[displayedProvider_].replacement = text;
        if (!text.isEmpty()) removeKey_->setEnabled(true);
    });
    connect(showKey_, &QCheckBox::toggled, this, [this](bool show) {
        apiKeyEdit_->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
    });
}

void SettingsDialog::updateProvider()
{
    displayedProvider_ = translatorCombo_->currentData().toString();
    const auto *info = TranslationProviderRegistry::find(displayedProvider_);
    if (!info) return;
    form_->setRowVisible(planCombo_, info->supportsPlan);
    form_->setRowVisible(baseUrlEdit_, info->supportsCustomEndpoint);
    form_->setRowVisible(modelEdit_, info->supportsModel);
    form_->setRowVisible(credentialActions_, info->requiresApiKey);
    form_->setRowVisible(keyEditor_, info->requiresApiKey && drafts_.value(displayedProvider_).editing);
    privacyLabel_->setVisible(info->requiresApiKey);
    const QSignalBlocker blocker(apiKeyEdit_);
    apiKeyEdit_->setText(drafts_.value(displayedProvider_).replacement);
    showKey_->setChecked(false);
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    errorLabel_->clear();
    updateCredentialStatus();
    adjustSize();
}

void SettingsDialog::updateCredentialStatus()
{
    const auto *info = TranslationProviderRegistry::find(displayedProvider_);
    if (!info || !info->requiresApiKey) return;
    QString error;
    const bool stored = !credentials_->loadSecret(displayedProvider_, &error).isEmpty();
    const bool environment = displayedProvider_ == QLatin1String("deepl")
        && !qEnvironmentVariable("DEEPL_API_KEY").trimmed().isEmpty();
    credentialStatus_->setText(drafts_.value(displayedProvider_).remove ? tr("Removal pending")
        : stored ? tr("Configured") : environment ? tr("Environment fallback") : tr("Not configured"));
    removeKey_->setEnabled(stored || !drafts_.value(displayedProvider_).replacement.isEmpty());
    if (!error.isEmpty()) errorLabel_->setText(error);
}

bool SettingsDialog::applyCredentials()
{
    errorLabel_->clear();
    const auto *info = TranslationProviderRegistry::find(displayedProvider_);
    if (info && info->supportsCustomEndpoint) {
        QString error;
        if (!OpenAiCompatibleTranslator::completionUrl(baseUrlEdit_->text(), &error).isValid()) {
            errorLabel_->setText(error);
            return false;
        }
        if (modelEdit_->text().trimmed().isEmpty()) {
            errorLabel_->setText(tr("Please configure a model."));
            return false;
        }
    }
    for (const auto &id : drafts_.keys()) {
        const auto draft = drafts_.value(id);
        QString error;
        const bool changed = draft.remove || !draft.replacement.trimmed().isEmpty();
        if (draft.replacement.contains(QLatin1Char('\n')) || draft.replacement.contains(QLatin1Char('\r'))) {
            errorLabel_->setText(tr("Invalid API key."));
            return false;
        }
        if (changed && !(draft.remove ? credentials_->removeSecret(id, &error)
                          : credentials_->saveSecret(id, draft.replacement.trimmed(), &error))) {
            errorLabel_->setText(error.isEmpty() ? tr("Credential operation failed.") : error);
            return false;
        }
        drafts_.remove(id);
        if (changed) settings_.notifyCredentialsChanged();
    }
    if (info && info->supportsPlan) settings_.setDeepLPlan(planCombo_->currentData().toString());
    settings_.setOpenAiBaseUrl(baseUrlEdit_->text());
    settings_.setOpenAiModel(modelEdit_->text());
    const QSignalBlocker blocker(apiKeyEdit_);
    apiKeyEdit_->clear();
    updateProvider();
    return true;
}

void SettingsDialog::clearDrafts()
{
    drafts_.clear();
    const QSignalBlocker blocker(apiKeyEdit_);
    apiKeyEdit_->clear();
    showKey_->setChecked(false);
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
}

void SettingsDialog::reject()
{
    clearDrafts();
    QDialog::reject();
}

void SettingsDialog::selectById(QComboBox *comboBox, const QString &id)
{
    const int index = comboBox->findData(id);
    comboBox->setCurrentIndex(index >= 0 ? index : 0);
}
