#pragma once
#include <QString>
#include <QMetaType>
#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

namespace Asr {
enum class State { Unloaded, Loading, Ready, Recognizing, Error };
enum class ResultKind { Partial, Final };
enum class ErrorCode { None, ModelNotLoaded, ModelLoadFailed, InvalidAudio,
    UnsupportedLanguage, UtteranceTooLong, Cancelled, Timeout, BackendFailure, EmptyAudio };
struct Error {
    ErrorCode code = ErrorCode::None;
    QString message;
};
struct Result {
    QString text;
    ResultKind kind = ResultKind::Final;
    quint64 session = 0, utterance = 0, revision = 0;
    QString detectedLanguage;
    qint64 processingMs = 0;
    bool discontinuity = false;
    qint64 audioDurationMs = 0;
};
struct Options {
    QString language = QStringLiteral("auto");
    int timeoutMs = 60000;
    int threads = 4;
};
struct Cancellation {
    std::atomic_bool cancelled{false};
    std::chrono::steady_clock::time_point deadline;
    bool expired() const { return std::chrono::steady_clock::now() >= deadline; }
    bool requested() const { return cancelled.load(std::memory_order_relaxed) || expired(); }
};
using CancelToken = std::shared_ptr<Cancellation>;
struct BackendResult {
    QString text, detectedLanguage;
    Error error;
};
inline bool supportedLanguage(const QString &language) {
    return language == QLatin1String("auto") || language == QLatin1String("en")
        || language == QLatin1String("zh") || language == QLatin1String("ja")
        || language == QLatin1String("ko");
}
}
Q_DECLARE_METATYPE(Asr::State)
Q_DECLARE_METATYPE(Asr::Result)
Q_DECLARE_METATYPE(Asr::Error)
