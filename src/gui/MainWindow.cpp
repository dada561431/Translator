#include "gui/MainWindow.h"

#include "config/SettingsManager.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(SettingsManager &settings, QWidget *parent)
    : QMainWindow(parent)
    , settings_(settings)
{
    setWindowTitle(QStringLiteral("Translator"));
    setMinimumSize(640, 480);
    resize(900, 700);

    createUi();
    loadSettings();
    connectSettings();
    setTranslationRunning(false);
    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::createUi()
{
    auto *centralWidget = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto *settingsGroup = new QGroupBox(tr("Settings"), centralWidget);
    auto *settingsLayout = new QFormLayout(settingsGroup);
    settingsLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    sourceLanguageCombo_ = new QComboBox(settingsGroup);
    sourceLanguageCombo_->setObjectName(QStringLiteral("sourceLanguageCombo"));
    sourceLanguageCombo_->addItem(tr("Auto Detect"), QStringLiteral("auto"));
    sourceLanguageCombo_->addItem(tr("Chinese"), QStringLiteral("zh"));
    sourceLanguageCombo_->addItem(tr("English"), QStringLiteral("en"));
    sourceLanguageCombo_->addItem(tr("Japanese"), QStringLiteral("ja"));
    sourceLanguageCombo_->addItem(tr("Korean"), QStringLiteral("ko"));

    targetLanguageCombo_ = new QComboBox(settingsGroup);
    targetLanguageCombo_->setObjectName(QStringLiteral("targetLanguageCombo"));
    targetLanguageCombo_->addItem(tr("Chinese"), QStringLiteral("zh"));
    targetLanguageCombo_->addItem(tr("English"), QStringLiteral("en"));
    targetLanguageCombo_->addItem(tr("Japanese"), QStringLiteral("ja"));
    targetLanguageCombo_->addItem(tr("Korean"), QStringLiteral("ko"));

    ocrEngineCombo_ = new QComboBox(settingsGroup);
    ocrEngineCombo_->setObjectName(QStringLiteral("ocrEngineCombo"));
    ocrEngineCombo_->addItem(tr("Windows OCR"), QStringLiteral("windows_ocr"));

    translatorCombo_ = new QComboBox(settingsGroup);
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

    settingsLayout->addRow(tr("Source Language"), sourceLanguageCombo_);
    settingsLayout->addRow(tr("Target Language"), targetLanguageCombo_);
    settingsLayout->addRow(tr("OCR Engine"), ocrEngineCombo_);
    settingsLayout->addRow(tr("Translator"), translatorCombo_);
    mainLayout->addWidget(settingsGroup);

    auto *controlsGroup = new QGroupBox(tr("Controls"), centralWidget);
    auto *controlsLayout = new QHBoxLayout(controlsGroup);

    selectRegionButton_ = new QPushButton(tr("Select Region"), controlsGroup);
    selectRegionButton_->setObjectName(QStringLiteral("selectRegionButton"));
    selectRegionButton_->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));

    startButton_ = new QPushButton(tr("Start Translation"), controlsGroup);
    startButton_->setObjectName(QStringLiteral("startTranslationButton"));
    startButton_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));

    stopButton_ = new QPushButton(tr("Stop"), controlsGroup);
    stopButton_->setObjectName(QStringLiteral("stopButton"));
    stopButton_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));

    controlsLayout->addWidget(selectRegionButton_);
    controlsLayout->addWidget(startButton_);
    controlsLayout->addWidget(stopButton_);
    controlsLayout->addStretch();
    mainLayout->addWidget(controlsGroup);

    auto *textSplitter = new QSplitter(Qt::Vertical, centralWidget);
    textSplitter->setObjectName(QStringLiteral("textSplitter"));
    textSplitter->setChildrenCollapsible(false);

    auto *originalGroup = new QGroupBox(tr("Original Text"), textSplitter);
    auto *originalLayout = new QVBoxLayout(originalGroup);
    originalTextEdit_ = new QPlainTextEdit(originalGroup);
    originalTextEdit_->setObjectName(QStringLiteral("originalTextEdit"));
    originalTextEdit_->setReadOnly(true);
    originalLayout->addWidget(originalTextEdit_);

    auto *translationGroup = new QGroupBox(tr("Translation"), textSplitter);
    auto *translationLayout = new QVBoxLayout(translationGroup);
    translationTextEdit_ = new QPlainTextEdit(translationGroup);
    translationTextEdit_->setObjectName(QStringLiteral("translationTextEdit"));
    translationTextEdit_->setReadOnly(true);
    translationLayout->addWidget(translationTextEdit_);

    textSplitter->addWidget(originalGroup);
    textSplitter->addWidget(translationGroup);
    textSplitter->setStretchFactor(0, 1);
    textSplitter->setStretchFactor(1, 1);
    mainLayout->addWidget(textSplitter, 1);

    setCentralWidget(centralWidget);
}

void MainWindow::loadSettings()
{
    selectById(sourceLanguageCombo_, settings_.sourceLanguage());
    selectById(targetLanguageCombo_, settings_.targetLanguage());
    selectById(ocrEngineCombo_, settings_.ocrEngine());
    selectById(translatorCombo_, settings_.translator());
}

void MainWindow::connectSettings()
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

    connect(selectRegionButton_, &QPushButton::clicked, this, [this] {
        statusBar()->showMessage(tr("Region selection is not implemented yet."));
    });
    connect(startButton_, &QPushButton::clicked, this, [this] {
        setTranslationRunning(true);
        statusBar()->showMessage(tr("Translation UI started. Backend is not implemented yet."));
    });
    connect(stopButton_, &QPushButton::clicked, this, [this] {
        setTranslationRunning(false);
        statusBar()->showMessage(tr("Stopped."));
    });
}

void MainWindow::setTranslationRunning(bool running)
{
    startButton_->setEnabled(!running);
    stopButton_->setEnabled(running);
}

void MainWindow::selectById(QComboBox *comboBox, const QString &id)
{
    const int index = comboBox->findData(id);
    comboBox->setCurrentIndex(index >= 0 ? index : 0);
}
