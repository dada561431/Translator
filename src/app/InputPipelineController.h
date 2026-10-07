#pragma once
#include "app/AudioTranslationCoordinator.h"
#include <functional>

// The OCR scheduler remains opaque; mode arbitration is not a shared scheduler.
class InputPipelineController final
{
public:
    enum class Mode { ScreenOcr, AudioMicrophone, AudioSystemLoopback };
    struct OcrActions { std::function<void()> start, stop; std::function<bool()> running; };
    InputPipelineController(AudioTranslationCoordinator &audio, OcrActions ocr)
        : audio_(audio), ocr_(std::move(ocr)) {}
    void select(Mode mode) { if (mode_ != mode) { stop(); mode_ = mode; } }
    bool start(AudioTranslationCoordinator::Configuration configuration = {}) {
        if (mode_ == Mode::ScreenOcr) { audio_.stop(); ocr_.start(); return ocr_.running(); }
        ocr_.stop();
        configuration.kind = mode_ == Mode::AudioMicrophone ? Audio::InputKind::Microphone : Audio::InputKind::SystemLoopback;
        return audio_.start(configuration);
    }
    void stop() { audio_.stop(); ocr_.stop(); }
    bool isRunning() const { return audio_.isRunning() || ocr_.running(); }
    Mode mode() const { return mode_; }
private:
    AudioTranslationCoordinator &audio_;
    OcrActions ocr_;
    Mode mode_ = Mode::ScreenOcr;
};
