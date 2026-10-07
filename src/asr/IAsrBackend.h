#pragma once
#include "asr/AsrTypes.h"

// All methods, including destruction, belong exclusively to the ASR worker.
class IAsrBackend
{
public:
    virtual ~IAsrBackend() = default;
    virtual Asr::Error loadModel(const QString &path, const Asr::CancelToken &cancel) = 0;
    virtual void unloadModel() = 0;
    virtual Asr::BackendResult recognize(const std::vector<float> &pcm,
        const Asr::Options &options, const Asr::CancelToken &cancel) = 0;
};
