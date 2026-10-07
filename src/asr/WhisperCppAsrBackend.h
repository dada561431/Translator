#pragma once
#include "asr/IAsrBackend.h"

struct whisper_context;
class WhisperCppAsrBackend final : public IAsrBackend
{
public:
    WhisperCppAsrBackend();
    ~WhisperCppAsrBackend() override;
    Asr::Error loadModel(const QString &path, const Asr::CancelToken &cancel) override;
    void unloadModel() override;
    Asr::BackendResult recognize(const std::vector<float> &pcm,
        const Asr::Options &options, const Asr::CancelToken &cancel) override;
private:
    whisper_context *context_ = nullptr;
};
