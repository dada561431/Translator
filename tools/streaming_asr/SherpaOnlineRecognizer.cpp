#include "SherpaOnlineRecognizer.h"
#include "sherpa-onnx/c-api/c-api.h"
#include <onnxruntime_c_api.h>
#include <QDir>
#include <QFileInfo>
#include <stdexcept>

namespace StreamingProbe {
namespace {
class Sherpa final : public IOnlineRecognizer {
public:
    Sherpa(QString dir, int threads, bool endpoints, QString model)
        : dir_(std::move(dir)), model_(std::move(model)), threads_(threads), endpoints_(endpoints) {}
    ~Sherpa() override {
        if (stream_) SherpaOnnxDestroyOnlineStream(stream_);
        if (recognizer_) SherpaOnnxDestroyOnlineRecognizer(recognizer_);
        if (env_) api_->ReleaseEnv(env_);
    }
    QJsonObject load() override {
        const QString version = QString::fromUtf8(SherpaOnnxGetVersionStr());
        if (version != QStringLiteral("1.13.8") || !QString::fromUtf8(SherpaOnnxGetGitSha1()).startsWith("11afbd00"))
            throw std::runtime_error("Expected pinned sherpa-onnx v1.13.8 / 11afbd00 runtime");
        const auto runtimeVersion = QString::fromUtf8(OrtGetApiBase()->GetVersionString());
        if (runtimeVersion != QStringLiteral("1.28.2"))
            throw std::runtime_error("Expected app-local ORT 1.28.2; an older DLL (e.g. System32 ORT 1.17.1) cannot supply API 28");
        api_ = OrtGetApiBase()->GetApi(28);
        if (!api_) throw std::runtime_error("ORT API 28 unavailable");
        auto checked = [&](OrtStatus *status) {
            if (!status) return;
            const std::string message = api_->GetErrorMessage(status);
            api_->ReleaseStatus(status); throw std::runtime_error(message);
        };
        checked(api_->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "StreamingProbe", &env_));
        checked(api_->DisableTelemetryEvents(env_));
        auto path = [&](const char *file) {
            QFileInfo info(QDir(dir_).filePath(QLatin1String(file)));
            if (!info.isFile() || !info.isReadable() || info.size() == 0)
                throw std::runtime_error("Missing/unreadable local model input");
            return info.absoluteFilePath().toUtf8();
        };
        if (model_ != "paraformer" && model_ != "zipformer") throw std::runtime_error("Unsupported QA model");
        const bool zip = model_ == "zipformer";
        const auto encoder = path(zip ? "encoder-epoch-99-avg-1.int8.onnx" : "encoder.int8.onnx");
        // Official Zipformer int8 recipe keeps its small decoder in FP32.
        const auto decoder = path(zip ? "decoder-epoch-99-avg-1.onnx" : "decoder.int8.onnx");
        const auto tokens = path("tokens.txt");
        const auto joiner = zip ? path("joiner-epoch-99-avg-1.int8.onnx") : QByteArray{};
        SherpaOnnxOnlineRecognizerConfig config{};
        config.feat_config.sample_rate = 16000; config.feat_config.feature_dim = 80;
        if (zip) {
            config.model_config.transducer.encoder = encoder.constData();
            config.model_config.transducer.decoder = decoder.constData();
            config.model_config.transducer.joiner = joiner.constData();
        } else {
            config.model_config.paraformer.encoder = encoder.constData();
            config.model_config.paraformer.decoder = decoder.constData();
        }
        config.model_config.tokens = tokens.constData(); config.model_config.provider = "cpu";
        config.model_config.num_threads = threads_; config.decoding_method = "greedy_search";
        config.enable_endpoint = endpoints_; config.rule1_min_trailing_silence = 2.4f;
        config.rule2_min_trailing_silence = 1.2f; config.rule3_min_utterance_length = 20.f;
        recognizer_ = SherpaOnnxCreateOnlineRecognizer(&config);
        if (!recognizer_) throw std::runtime_error("SherpaOnnxCreateOnlineRecognizer failed");
        stream_ = SherpaOnnxCreateOnlineStream(recognizer_);
        if (!stream_) throw std::runtime_error("SherpaOnnxCreateOnlineStream failed");
        return {{"version", version}, {"commit", QString::fromUtf8(SherpaOnnxGetGitSha1())},
            {"onnxruntime", runtimeVersion},
            {"sherpa_ort_build_metadata", QString::fromUtf8(SherpaOnnxGetOnnxruntimeVersionStr())},
            {"ort_telemetry_disabled", true},
            {"model", model_}, {"decoding_method", "greedy_search"},
            {"provider", "cpu"}, {"threads", threads_}, {"endpoint_enabled", endpoints_}};
    }
    void accept(const std::vector<float> &samples) override {
        SherpaOnnxOnlineStreamAcceptWaveform(stream_, 16000, samples.data(), int32_t(samples.size()));
    }
    bool ready() override { return SherpaOnnxIsOnlineStreamReady(recognizer_, stream_); }
    void decode() override { SherpaOnnxDecodeOnlineStream(recognizer_, stream_); }
    QString text() override {
        const auto *result = SherpaOnnxGetOnlineStreamResult(recognizer_, stream_);
        if (!result) throw std::runtime_error("Sherpa returned a null result");
        const auto text = QString::fromUtf8(result->text);
        SherpaOnnxDestroyOnlineRecognizerResult(result); return text;
    }
    bool endpoint() override { return SherpaOnnxOnlineStreamIsEndpoint(recognizer_, stream_); }
    void reset() override { SherpaOnnxOnlineStreamReset(recognizer_, stream_); }
    void finish() override { SherpaOnnxOnlineStreamInputFinished(stream_); }
private:
    QString dir_, model_;
    int threads_;
    bool endpoints_;
    const SherpaOnnxOnlineRecognizer *recognizer_ = nullptr;
    const SherpaOnnxOnlineStream *stream_ = nullptr;
    const OrtApi *api_ = nullptr;
    OrtEnv *env_ = nullptr;
};
}
std::unique_ptr<IOnlineRecognizer> createSherpa(const QString &modelDir, int threads, bool endpoints, const QString &model) {
    return std::make_unique<Sherpa>(modelDir, threads, endpoints, model);
}
}
