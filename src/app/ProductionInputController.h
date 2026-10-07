#pragma once
#include "app/InputPipelineController.h"
#include "config/SettingsManager.h"

// Production lifecycle around the already tested, mutually exclusive pipelines.
class ProductionInputController final : public QObject {
    Q_OBJECT
public:
    ProductionInputController(SettingsManager &settings, AudioInputCoordinator &capture,
        AsrCoordinator &asr, AudioTranslationCoordinator &audio,
        InputPipelineController::OcrActions ocr, bool available, QObject *parent = nullptr);
    ~ProductionInputController() override;
    void start();
    void stop();
    bool isRunning() const { return pendingStart_ || pipelines_.isRunning(); }
    bool isScreen() const { return settings_.inputMode() == QLatin1String("screen"); }
    static QString modelPath(const QString &configured, const QString &applicationDir);
signals:
    void stateChanged(bool running);
    void feedback(const QString &message);
private:
    void selectMode();
    void beginCapture();
    SettingsManager &settings_;
    AudioInputCoordinator &capture_;
    AsrCoordinator &asr_;
    InputPipelineController pipelines_;
    AudioTranslationCoordinator::Configuration configuration_;
    bool available_, pendingStart_ = false;
    QString loadedModel_;
};
