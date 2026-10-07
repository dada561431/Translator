#pragma once
#include "StreamingProbeCore.h"
namespace StreamingProbe {
std::unique_ptr<IOnlineRecognizer> createSherpa(const QString &modelDir, int threads, bool endpoints);
}
