#include "gui/SettingsDialog.h"

#include "config/SettingsManager.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(SettingsManager &settings, QWidget *parent)
    : QDialog(parent)
    , settings_(settings)
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("Translator Settings"));
    setModal(false);
    setMinimumWidth(420);

    createUi();
    loadSettings();
    connectSettings();
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    loadSettings();
    QDialog::showEvent(event);
}

void SettingsDialog::createUi()
{
    auto *layout = new QVBoxLayout(this);
    auto *formLayout = new QFormLayout();
    formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    sourceLanguageCombo_ = new QComboBox(this);
    sourceLanguageCombo_->setObjectName(QStringLiteral("sourceLanguageCombo"));
    sourceLanguageCombo_->addItem(tr("Auto Detect"), QStringLiteral("auto"));
    sourceLanguageCombo_->addItem(tr("Chinese"), QStringLiteral("zh"));
    sourceLanguageCombo_->addItem(tr("English"), QStringLiteral("en"));
    sourceLanguageCombo_->addItem(tr("Japanese"), QStringLiteral("ja"));
    sourceLanguageCombo_->addItem(tr("Korean"), QStringLiteral("ko"));

    targetLanguageCombo_ = new QComboBox(this);
    targetLanguageCombo_->setObjectName(QStringLiteral("targetLanguageCombo"));
    targetLanguageCombo_->addItem(tr("Chinese"), QStringLiteral("zh"));
    targetLanguageCombo_->addItem(tr("English"), QStringLiteral("en"));
    targetLanguageCombo_->addItem(tr("Japanese"), QStringLiteral("ja"));
    targetLanguageCombo_->addItem(tr("Korean"), QStringLiteral("ko"));

    ocrEngineCombo_ = new QComboBox(this);
    ocrEngineCombo_->setObjectName(QStringLiteral("ocrEngineCombo"));
    ocrEngineCombo_->addItem(tr("Windows OCR"), QStringLiteral("windows_ocr"));

    translatorCombo_ = new QComboBox(this);
    translatorCombo_->setObjectName(QStringLiteral("translatorCombo"));
    translatorCombo_->addItem(tr("Not Configured"), QStringLiteral("none"));

    const QList<QComboBox *> comboBoxes = {
        sourceLanguageCombo_,
        targetLanguageCombo_,
        ocrEngineCombo_,
        translatorCombo_,
    };
    for (QComboBox *comboBox : comboBoxes) {
        comboBox->setMinimumContentsLength(18);
        comboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    formLayout->addRow(tr("Source Language"), sourceLanguageCombo_);
    formLayout->addRow(tr("Target Language"), targetLanguageCombo_);
    formLayout->addRow(tr("OCR Engine"), ocrEngineCombo_);
    formLayout->addRow(tr("Translator"), translatorCombo_);
    layout->addLayout(formLayout);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttonBox->setObjectName(QStringLiteral("settingsButtonBox"));
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttonBox);
}

void SettingsDialog::loadSettings()
{
    const QSignalBlocker sourceBlocker(sourceLanguageCombo_);
    const QSignalBlocker targetBlocker(targetLanguageCombo_);
    const QSignalBlocker ocrBlocker(ocrEngineCombo_);
    const QSignalBlocker translatorBlocker(translatorCombo_);

    selectById(sourceLanguageCombo_, settings_.sourceLanguage());
    selectById(targetLanguageCombo_, settings_.targetLanguage());
    selectById(ocrEngineCombo_, settings_.ocrEngine());
    selectById(translatorCombo_, settings_.translator());
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
    });
}

void SettingsDialog::selectById(QComboBox *comboBox, const QString &id)
{
    const int index = comboBox->findData(id);
    comboBox->setCurrentIndex(index >= 0 ? index : 0);
}
