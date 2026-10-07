#include "gui/SettingsDialog.h"
#include "config/SettingsManager.h"
#include "translator/TranslationProviderRegistry.h"
#include "translator/OpenAiCompatibleTranslator.h"
#include "platform/WindowCaptureExclusion.h"

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
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QKeySequenceEdit>
#include <QScrollArea>
#include <QScreen>
#include <QFileDialog>

SettingsDialog::SettingsDialog(SettingsManager &settings, QWidget *parent, ICredentialStore *credentials)
    : QDialog(parent), settings_(settings)
{
    if (!credentials) ownedCredentials_ = createPlatformCredentialStore();
    credentials_ = credentials ? credentials : ownedCredentials_.get();
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("Translator Settings"));
    setModal(false);
    setMinimumWidth(460);
    resize(520, 640);
    createUi();
    loadSettings();
    connectSettings();
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    clearDrafts();
    loadSettings();
    QDialog::showEvent(event);
    if (screen()) setMaximumHeight(qMax(300, screen()->availableGeometry().height() - 60));
}

void SettingsDialog::createUi()
{
    auto *layout = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget(scroll);
    auto *content = new QVBoxLayout(body);
    content->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(body);
    layout->addWidget(scroll);
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
    ocrEngineCombo_->addItem(tr("PP-OCRv6 Small"), QStringLiteral("paddle-small"));
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
    content->addLayout(form_);
    privacyLabel_ = new QLabel(tr("识别出的文字将发送至所选翻译服务。"), this);
    privacyLabel_->setWordWrap(true);
    content->addWidget(privacyLabel_);
    auto *audioForm = new QFormLayout();
    inputModeCombo_ = new QComboBox(this);
    inputModeCombo_->setObjectName(QStringLiteral("inputModeSettingsCombo"));
    inputModeCombo_->addItem(tr("Screen"), "screen");
    inputModeCombo_->addItem(tr("Microphone"), "microphone");
    inputModeCombo_->addItem(tr("System Audio"), "system-audio");
    microphoneCombo_ = new QComboBox(this);
    microphoneCombo_->setObjectName(QStringLiteral("microphoneDeviceCombo"));
    outputCombo_ = new QComboBox(this);
    outputCombo_->setObjectName(QStringLiteral("outputDeviceCombo"));
    speechLanguageCombo_ = new QComboBox(this);
    speechLanguageCombo_->setObjectName(QStringLiteral("speechLanguageCombo"));
    const QStringList speechNames{tr("Auto"), tr("English"), tr("Chinese"), tr("Japanese"), tr("Korean")};
    const QStringList speechIds{"auto", "en", "zh", "ja", "ko"};
    for (int i = 0; i < speechIds.size(); ++i) speechLanguageCombo_->addItem(speechNames[i], speechIds[i]);
    auto *modelRow = new QWidget(this);
    auto *modelLayout = new QHBoxLayout(modelRow);
    modelLayout->setContentsMargins(0, 0, 0, 0);
    speechModelEdit_ = new QLineEdit(modelRow);
    speechModelEdit_->setObjectName(QStringLiteral("speechModelEdit"));
    speechModelEdit_->setPlaceholderText(QStringLiteral("models/ggml-base.bin"));
    auto *browse = new QPushButton(tr("Browse..."), modelRow);
    browse->setObjectName(QStringLiteral("speechModelBrowse"));
    modelLayout->addWidget(speechModelEdit_, 1); modelLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Speech model"), speechModelEdit_->text(), tr("Whisper models (*.bin);;All files (*)"));
        if (!path.isEmpty()) speechModelEdit_->setText(path);
    });
    audioForm->addRow(tr("Input mode"), inputModeCombo_);
    audioForm->addRow(tr("Microphone Device"), microphoneCombo_);
    audioForm->addRow(tr("Output Device"), outputCombo_);
    audioForm->addRow(tr("ASR Model"), modelRow);
    audioForm->addRow(tr("Speech Recognition Language"), speechLanguageCombo_);
    audioForm->addRow(tr("Speech Detection"), new QLabel(tr("Automatic"), this));
    content->addLayout(audioForm);
    auto *overlayForm = new QFormLayout();
    translationFontSize_ = new QDoubleSpinBox(this);
    translationFontSize_->setObjectName(QStringLiteral("translationFontSizeSpin"));
    originalFontSize_ = new QDoubleSpinBox(this);
    originalFontSize_->setObjectName(QStringLiteral("originalFontSizeSpin"));
    for (auto *spin : {translationFontSize_, originalFontSize_}) {
        spin->setRange(10, 72);
        spin->setDecimals(1);
        spin->setSuffix(tr(" pt"));
    }
    backgroundOpacity_ = new QSpinBox(this);
    backgroundOpacity_->setObjectName(QStringLiteral("backgroundOpacitySpin"));
    backgroundOpacity_->setRange(0, 100);
    backgroundOpacity_->setSuffix(QStringLiteral(" %"));
    showTranslation_ = new QCheckBox(tr("Show Translation"), this);
    showTranslation_->setObjectName(QStringLiteral("showTranslationCheck"));
    showOriginal_ = new QCheckBox(tr("Show Original"), this);
    showOriginal_->setObjectName(QStringLiteral("showOriginalCheck"));
    excludeFromCapture_ = new QCheckBox(tr("Exclude subtitle overlay from screen capture"), this);
    excludeFromCapture_->setObjectName(QStringLiteral("excludeFromCaptureCheck"));
    captureExclusionNote_ = new QLabel(this);
    captureExclusionNote_->setObjectName(QStringLiteral("captureExclusionNote"));
    captureExclusionNote_->setWordWrap(true);
    overlayForm->addRow(tr("Translation font size"), translationFontSize_);
    overlayForm->addRow(tr("Original font size"), originalFontSize_);
    overlayForm->addRow(tr("Subtitle background opacity"), backgroundOpacity_);
    overlayForm->addRow(showTranslation_);
    overlayForm->addRow(showOriginal_);
    overlayForm->addRow(excludeFromCapture_);
    overlayForm->addRow(captureExclusionNote_);
    dragLocked_ = new QCheckBox(tr("Lock overlay position"), this);
    dragLocked_->setObjectName(QStringLiteral("dragLockedCheck"));
    overlayForm->addRow(dragLocked_);
    const QStringList labels{tr("Toggle interaction"), tr("Select region"), tr("Start / Stop")};
    const QStringList names{QStringLiteral("toggleInteractionHotkey"), QStringLiteral("regionHotkey"), QStringLiteral("startStopHotkey")};
    for (int i = 0; i < 3; ++i) {
        hotkeyEdits_[i] = new QKeySequenceEdit(this);
        hotkeyEdits_[i]->setObjectName(names[i]);
        overlayForm->addRow(labels[i], hotkeyEdits_[i]);
    }
    content->addLayout(overlayForm);
    setCaptureExclusionAvailable(WindowCaptureExclusion::platformSupported());
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
    sourceLanguageCombo_->setItemText(0, settings_.ocrEngine() == QLatin1String("tesseract")
                                     ? tr("Auto (English OCR)") : tr("Auto"));
    selectById(translatorCombo_, settings_.translator());
    selectById(planCombo_, settings_.deepLEndpoint().contains(QStringLiteral("api.deepl.com"))
                           ? QStringLiteral("pro") : QStringLiteral("free"));
    baseUrlEdit_->setText(settings_.openAiBaseUrl());
    modelEdit_->setText(settings_.openAiModel());
    loadOverlaySettings();
    loadAudioSettings();
    updateProvider();
}

void SettingsDialog::loadAudioSettings()
{
    const auto saved = settings_.audioSettings();
    selectById(inputModeCombo_, settings_.inputMode());
    selectById(speechLanguageCombo_, saved.language);
    speechModelEdit_->setText(saved.modelPath);
    auto populate = [this](QComboBox *combo, Audio::InputKind kind, const QByteArray &id) {
        combo->clear();
        combo->addItem(kind == Audio::InputKind::Microphone ? tr("Default") : tr("Default Output"), QByteArray());
        if (enumerateAudio_) for (const auto &device : enumerateAudio_(kind)) combo->addItem(device.description, device.id);
        int index = combo->findData(id);
        if (index < 0) { combo->addItem(tr("Configured device unavailable"), id); index = combo->count() - 1; }
        combo->setCurrentIndex(index);
    };
    populate(microphoneCombo_, Audio::InputKind::Microphone, saved.microphoneId);
    populate(outputCombo_, Audio::InputKind::SystemLoopback, saved.outputId);
}

void SettingsDialog::loadOverlaySettings()
{
    const auto appearance = settings_.overlayAppearance();
    const QSignalBlocker translation(showTranslation_), original(showOriginal_);
    translationFontSize_->setValue(appearance.translationFontSize);
    originalFontSize_->setValue(appearance.originalFontSize);
    backgroundOpacity_->setValue(appearance.backgroundOpacity);
    showTranslation_->setChecked(appearance.showTranslation);
    showOriginal_->setChecked(appearance.showOriginal);
    excludeFromCapture_->setChecked(settings_.overlayExcludeFromCapture());
    dragLocked_->setChecked(settings_.overlayDragLocked());
    const auto hotkeys = settings_.globalHotkeys();
    for (int i = 0; i < 3; ++i)
        hotkeyEdits_[i]->setKeySequence(QKeySequence(hotkeys.shortcuts[i], QKeySequence::PortableText));
    updateVisibilityChecks();
}

void SettingsDialog::updateVisibilityChecks()
{
    showTranslation_->setEnabled(showOriginal_->isChecked());
    showOriginal_->setEnabled(showTranslation_->isChecked());
}

void SettingsDialog::setCaptureExclusionAvailable(bool available)
{
    excludeFromCapture_->setEnabled(available);
    captureExclusionNote_->setText(available ? tr("Best-effort Windows capture exclusion.")
                                           : tr("Capture exclusion unavailable"));
}

void SettingsDialog::connectSettings()
{
    connect(showTranslation_, &QCheckBox::toggled, this, &SettingsDialog::updateVisibilityChecks);
    connect(showOriginal_, &QCheckBox::toggled, this, &SettingsDialog::updateVisibilityChecks);
    connect(sourceLanguageCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        settings_.setSourceLanguage(sourceLanguageCombo_->itemData(index).toString());
    });
    connect(targetLanguageCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        settings_.setTargetLanguage(targetLanguageCombo_->itemData(index).toString());
    });
    connect(ocrEngineCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        settings_.setOcrEngine(ocrEngineCombo_->itemData(index).toString());
        sourceLanguageCombo_->setItemText(0, settings_.ocrEngine() == QLatin1String("tesseract")
                                         ? tr("Auto (English OCR)") : tr("Auto"));
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
    GlobalHotkeyConfig requested, canonical;
    std::array<GlobalHotkeyChord, 3> chords;
    QString hotkeyError;
    for (int i = 0; i < 3; ++i) requested.shortcuts[i] = hotkeyEdits_[i]->keySequence().toString(QKeySequence::PortableText);
    if (!parseGlobalHotkeys(requested, canonical, chords, hotkeyError)) {
        errorLabel_->setText(hotkeyError); return false;
    }
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
    if (hotkeyApply_) {
        if (!hotkeyApply_(canonical, hotkeyError)) { errorLabel_->setText(hotkeyError); return false; }
    } else if (canonical.shortcuts != settings_.globalHotkeys().shortcuts) {
        errorLabel_->setText(tr("Global shortcut registration unavailable.")); return false;
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
    // Plan already saves on explicit combo changes. An appearance-only Apply
    // must not materialize absent provider keys and invalidate the pipeline.
    if (baseUrlEdit_->text().trimmed() != settings_.openAiBaseUrl())
        settings_.setOpenAiBaseUrl(baseUrlEdit_->text());
    if (modelEdit_->text().trimmed() != settings_.openAiModel())
        settings_.setOpenAiModel(modelEdit_->text());
    settings_.setOverlayAppearance({translationFontSize_->value(), originalFontSize_->value(),
        backgroundOpacity_->value(), showTranslation_->isChecked(), showOriginal_->isChecked()});
    if (excludeFromCapture_->isEnabled())
        settings_.setOverlayExcludeFromCapture(excludeFromCapture_->isChecked());
    settings_.setOverlayDragLocked(dragLocked_->isChecked());
    settings_.setAudioSettings({microphoneCombo_->currentData().toByteArray(), outputCombo_->currentData().toByteArray(),
        speechModelEdit_->text(), speechLanguageCombo_->currentData().toString()});
    settings_.setInputMode(inputModeCombo_->currentData().toString());
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
    loadOverlaySettings();
    loadAudioSettings();
    QDialog::reject();
}

void SettingsDialog::selectById(QComboBox *comboBox, const QString &id)
{
    const int index = comboBox->findData(id);
    comboBox->setCurrentIndex(index >= 0 ? index : 0);
}
