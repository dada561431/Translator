#include "asr/WhisperCppAsrBackend.h"
#include <whisper.h>
#include <QFile>
#include <QLoggingCategory>
#include <QtEndian>
#include <mutex>
#include <cstring>
#include <stdexcept>

Q_DECLARE_LOGGING_CATEGORY(asrLog)
namespace {
void whisperLog(enum ggml_log_level level, const char *text, void *)
{
    const auto line = QString::fromUtf8(text).trimmed();
    if (line.isEmpty()) return;
    if (level >= GGML_LOG_LEVEL_ERROR) qCWarning(asrLog).noquote() << line;
    else qCDebug(asrLog).noquote() << line;
}
struct Loader {
    QFile file;
    Asr::CancelToken cancel;
    bool eof = false;
    QString failure;
};
bool abortInference(void *data) { return static_cast<Asr::Cancellation *>(data)->requested(); }
bool beginEncoder(whisper_context *, whisper_state *, void *data) { return !abortInference(data); }
}
WhisperCppAsrBackend::WhisperCppAsrBackend()
{
    static std::once_flag once;
    std::call_once(once, [] { whisper_log_set(whisperLog, nullptr); });
}
WhisperCppAsrBackend::~WhisperCppAsrBackend() { unloadModel(); }
void WhisperCppAsrBackend::unloadModel()
{
    if (context_) { whisper_free(context_); context_ = nullptr; }
}
Asr::Error WhisperCppAsrBackend::loadModel(const QString &path, const Asr::CancelToken &cancel)
{
    unloadModel();
    Loader loader; loader.file.setFileName(path); loader.cancel = cancel;
    if (!loader.file.open(QIODevice::ReadOnly))
        return {Asr::ErrorCode::ModelLoadFailed, QStringLiteral("Model file not found or unreadable: %1").arg(loader.file.errorString())};
    // Reject truncated/inconsistent headers before upstream's model-shape assertions.
    const auto header = loader.file.peek(48);
    auto word = [&](int i) { return qFromLittleEndian<qint32>(header.constData() + i * 4); };
    if (header.size() != 48 || loader.file.size() < 1048576 || word(0) != 0x67676d6c || word(2) != 1500
        || word(7) != word(3) || word(6) != 448 || word(1) < 50000 || word(1) > 53000
        || (word(10) != 80 && word(10) != 128) || word(4) <= 0 || word(8) <= 0
        || word(3) % word(4) || word(7) % word(8)
        || (word(3) != 384 && word(3) != 512 && word(3) != 768 && word(3) != 1024 && word(3) != 1280)
        || word(5) < 1 || word(5) > 32 || word(9) < 1 || word(9) > 32)
        return {Asr::ErrorCode::ModelLoadFailed, QStringLiteral("Invalid or damaged ggml Whisper model header.")};
    whisper_model_loader api{};
    api.context = &loader;
    api.read = [](void *data, void *output, size_t size) -> size_t {
        auto &l = *static_cast<Loader *>(data);
        if (l.cancel->requested()) throw std::runtime_error("Model loading cancelled or timed out.");
        const auto count = l.file.read(static_cast<char *>(output), qint64(size));
        // Upstream probes three tensor-header words past the final tensor before
        // consulting eof(). Match std::ifstream EOF-after-read, not QFile::atEnd().
        if (count == 0 && l.file.atEnd()) {
            l.eof = true; std::memset(output, 0, size); return 0;
        }
        if (count != qint64(size)) {
            l.failure = QStringLiteral("Truncated or unreadable Whisper model at byte %1.").arg(l.file.pos());
            throw std::runtime_error("Truncated or unreadable Whisper model.");
        }
        return size;
    };
    api.eof = [](void *data) { return static_cast<Loader *>(data)->eof; };
    api.close = [](void *data) { static_cast<Loader *>(data)->file.close(); };
    auto params = whisper_context_default_params();
    params.use_gpu = false; params.flash_attn = false;
    try { context_ = whisper_init_with_params(&api, params); }
    catch (const std::exception &e) {
        return {Asr::ErrorCode::ModelLoadFailed, QString::fromUtf8(e.what())};
    }
    if (!context_) return {Asr::ErrorCode::ModelLoadFailed, loader.failure.isEmpty()
        ? QStringLiteral("whisper.cpp could not load model.") : loader.failure};
    if (cancel->requested()) { unloadModel(); return {Asr::ErrorCode::Cancelled, QStringLiteral("Model load cancelled.")}; }
    qCInfo(asrLog) << "whisper.cpp CPU context loaded; GPU/VAD/translation disabled";
    return {};
}
Asr::BackendResult WhisperCppAsrBackend::recognize(const std::vector<float> &pcm,
    const Asr::Options &options, const Asr::CancelToken &cancel)
{
    using Asr::ErrorCode;
    if (!context_) return {{}, {}, {ErrorCode::ModelNotLoaded, QStringLiteral("Model not loaded.")}};
    if (!Asr::supportedLanguage(options.language))
        return {{}, {}, {ErrorCode::UnsupportedLanguage, QStringLiteral("Unsupported ASR language.")}};
    if (pcm.empty() || pcm.size() > 480000)
        return {{}, {}, {ErrorCode::InvalidAudio, QStringLiteral("Expected 1-480000 float samples at 16 kHz mono.")}};
    const auto language = options.language.toUtf8();
    if (options.language != QLatin1String("auto") && whisper_lang_id(language.constData()) < 0)
        return {{}, {}, {ErrorCode::UnsupportedLanguage, QStringLiteral("Language not supported by whisper.cpp.")}};
    if (!whisper_is_multilingual(context_) && options.language != QLatin1String("en")
        && options.language != QLatin1String("auto"))
        return {{}, {}, {ErrorCode::UnsupportedLanguage, QStringLiteral("English-only model cannot transcribe selected language.")}};
    auto params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads = options.threads;
    params.translate = false; params.no_context = true; params.no_timestamps = true;
    params.print_special = params.print_progress = params.print_realtime = params.print_timestamps = false;
    params.language = language.constData(); params.detect_language = false;
    params.abort_callback = abortInference; params.abort_callback_user_data = cancel.get();
    params.encoder_begin_callback = beginEncoder; params.encoder_begin_callback_user_data = cancel.get();
    params.vad = false;
    const int status = whisper_full(context_, params, pcm.data(), int(pcm.size()));
    if (cancel->requested()) return {{}, {}, {ErrorCode::Cancelled, QStringLiteral("Inference cooperatively cancelled.")}};
    if (status != 0) return {{}, {}, {ErrorCode::BackendFailure, QStringLiteral("whisper_full failed (%1).").arg(status)}};
    QString text;
    for (int i = 0; i < whisper_full_n_segments(context_); ++i)
        text += QString::fromUtf8(whisper_full_get_segment_text(context_, i));
    const int id = whisper_full_lang_id(context_);
    return {text.trimmed(), id >= 0 ? QString::fromUtf8(whisper_lang_str(id)) : QString(), {}};
}
