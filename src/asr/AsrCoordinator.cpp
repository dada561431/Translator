#include "asr/AsrCoordinator.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(asrLog, "translator.asr", QtInfoMsg)
namespace {
class AsrWorker final : public QObject {
public:
    explicit AsrWorker(AsrCoordinator::Factory factory) : factory_(std::move(factory)) {}
    ~AsrWorker() override { if (backend_) backend_->unloadModel(); }
    IAsrBackend *backend() {
        if (!backend_ && factory_) backend_ = factory_();
        return backend_.get();
    }
private:
    AsrCoordinator::Factory factory_;
    std::unique_ptr<IAsrBackend> backend_;
};
}
AsrCoordinator::AsrCoordinator(Factory factory, QObject *parent)
    : QObject(parent), worker_(new AsrWorker(std::move(factory)))
{
    qRegisterMetaType<Asr::Result>(); qRegisterMetaType<Asr::Error>();
    qRegisterMetaType<Asr::State>();
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        if (!busy_ || !token_ || token_->cancelled.exchange(true)) return;
        invalidate();
        ready_ = !loadedPath_.isEmpty();
        setState(Asr::State::Error);
        emit errorOccurred({Asr::ErrorCode::Timeout, QStringLiteral("ASR deadline exceeded; late results suppressed.")});
    });
    thread_.start();
}
AsrCoordinator::~AsrCoordinator()
{
    assertOwner(); invalidate();
    thread_.quit(); thread_.wait();
}
void AsrCoordinator::assertOwner() const { Q_ASSERT(thread() == QThread::currentThread()); }
void AsrCoordinator::setState(Asr::State state)
{
    if (state_ == state) return;
    state_ = state; emit stateChanged(state);
}
bool AsrCoordinator::fail(Asr::ErrorCode code, const QString &message)
{
    emit errorOccurred({code, message}); return false;
}
void AsrCoordinator::invalidate()
{
    ++session_; accepting_ = finalizing_ = false; pending_.reset(); buffer_.reset();
    if (token_) token_->cancelled = true;
    deadline_.stop();
}
void AsrCoordinator::loadModel(const QString &path)
{
    assertOwner();
    const auto normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (path.isEmpty()) { fail(Asr::ErrorCode::ModelLoadFailed, QStringLiteral("Model file not found.")); return; }
    if (normalized == desiredPath_ && (modelOperation_ || state_ == Asr::State::Loading || ready_)) return;
    invalidate(); ready_ = false; desiredPath_ = normalized; modelOperation_ = true;
    setState(Asr::State::Loading); dispatch();
}
void AsrCoordinator::unloadModel()
{
    assertOwner(); invalidate(); ready_ = false; desiredPath_.clear(); modelOperation_ = true;
    setState(Asr::State::Unloaded); dispatch();
}
void AsrCoordinator::stop() { unloadModel(); }
bool AsrCoordinator::beginUtterance(Asr::Options options)
{
    assertOwner();
    if (!ready_) return fail(Asr::ErrorCode::ModelNotLoaded, QStringLiteral("Load a model before beginning an utterance."));
    if (!Asr::supportedLanguage(options.language))
        return fail(Asr::ErrorCode::UnsupportedLanguage, QStringLiteral("Supported languages: auto, en, zh, ja, ko."));
    if (options.timeoutMs <= 0 || options.threads < 1 || options.threads > 64)
        return fail(Asr::ErrorCode::InvalidAudio, QStringLiteral("Invalid timeout or CPU thread count."));
    invalidate(); ++utterance_; revision_ = 0; options_ = std::move(options); accepting_ = true;
    setState(Asr::State::Ready); return true;
}
bool AsrCoordinator::pushPcm(const Audio::PcmChunk &chunk)
{
    assertOwner();
    if (!accepting_) return fail(Asr::ErrorCode::InvalidAudio, QStringLiteral("No open utterance accepts PCM."));
    const auto error = buffer_.append(chunk);
    if (error.code != Asr::ErrorCode::None) { emit errorOccurred(error); return false; }
    return true;
}
bool AsrCoordinator::queue(Asr::ResultKind kind)
{
    if (!accepting_) return fail(Asr::ErrorCode::InvalidAudio, QStringLiteral("No open utterance."));
    if (!buffer_.size()) return fail(Asr::ErrorCode::EmptyAudio, QStringLiteral("Utterance contains no PCM."));
    pending_ = Job{session_, utterance_, ++revision_, kind, buffer_.snapshot(), options_, buffer_.discontinuity()};
    if (kind == Asr::ResultKind::Final) {
        accepting_ = false; finalizing_ = true;
        if (token_) token_->cancelled = true;
    }
    dispatch(); return true;
}
bool AsrCoordinator::requestPartial() { assertOwner(); return queue(Asr::ResultKind::Partial); }
bool AsrCoordinator::finalizeUtterance() { assertOwner(); return queue(Asr::ResultKind::Final); }
void AsrCoordinator::cancelUtterance()
{
    assertOwner(); invalidate();
    setState(ready_ ? Asr::State::Ready : Asr::State::Unloaded);
}
void AsrCoordinator::dispatch()
{
    if (busy_ || (!modelOperation_ && !pending_)) return;
    busy_ = true;
    const auto op = ++operation_, generation = session_;
    const bool model = modelOperation_;
    modelOperation_ = false;
    if (model) loadedPath_.clear();
    const auto path = desiredPath_;
    const auto job = model ? std::optional<Job>{} : std::move(pending_);
    if (!model) pending_.reset();
    token_ = std::make_shared<Asr::Cancellation>();
    const auto token = token_;
    const int timeout = model ? 120000 : job->options.timeoutMs;
    token->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    deadline_.start(timeout);
    if (!model) setState(Asr::State::Recognizing);
    auto *worker = static_cast<AsrWorker *>(worker_);
    QMetaObject::invokeMethod(worker, [this, worker, op, generation, model, path, job, token] {
        QElapsedTimer elapsed; elapsed.start();
        Asr::BackendResult result;
        try {
            auto *backend = worker->backend();
            if (!backend) result.error = {Asr::ErrorCode::BackendFailure, QStringLiteral("ASR backend unavailable.")};
            else if (model) {
                backend->unloadModel();
                if (!path.isEmpty() && !token->requested()) result.error = backend->loadModel(path, token);
                if (token->requested()) backend->unloadModel();
            } else if (!token->requested()) {
                result = backend->recognize(AsrAudioBuffer::toFloat(job->pcm), job->options, token);
            }
        } catch (const std::exception &e) {
            result.error = {Asr::ErrorCode::BackendFailure, QString::fromUtf8(e.what())};
        } catch (...) {
            result.error = {Asr::ErrorCode::BackendFailure, QStringLiteral("Unknown ASR backend exception.")};
        }
        const auto ms = elapsed.elapsed();
        QMetaObject::invokeMethod(this, [this, op, generation, model, path, job, token, result, ms] {
            if (op != operation_) return;
            busy_ = false; deadline_.stop(); token_.reset();
            if (generation == session_ && !token->cancelled) {
                if (token->expired()) {
                    invalidate(); setState(Asr::State::Error);
                    emit errorOccurred({Asr::ErrorCode::Timeout, QStringLiteral("ASR deadline exceeded; result suppressed.")});
                } else if (result.error.code != Asr::ErrorCode::None) {
                    if (model) { loadedPath_.clear(); ready_ = false; }
                    accepting_ = finalizing_ = false; pending_.reset();
                    setState(Asr::State::Error); emit errorOccurred(result.error);
                } else if (model) {
                    loadedPath_ = path; ready_ = !path.isEmpty();
                    setState(ready_ ? Asr::State::Ready : Asr::State::Unloaded);
                    if (ready_ && generation == session_) { qCInfo(asrLog) << "Model loaded on worker in" << ms << "ms"; emit modelLoaded(ms); }
                } else if (job->utterance == utterance_ && !(finalizing_ && job->kind == Asr::ResultKind::Partial)) {
                    const Asr::Result value{result.text.trimmed(), job->kind, job->session, job->utterance,
                        job->revision, result.detectedLanguage, ms, job->discontinuity, job->pcm.size() / 32};
                    if (job->kind == Asr::ResultKind::Final) { finalizing_ = false; buffer_.reset(); }
                    setState(Asr::State::Ready);
                    if (generation == session_) emit resultReady(value);
                }
            }
            // Signals may synchronously cancel/reload/start; dispatch only the current pending work.
            dispatch();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
