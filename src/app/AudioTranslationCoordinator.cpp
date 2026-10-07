#include "app/AudioTranslationCoordinator.h"
#include <QThread>

AudioTranslationCoordinator::AudioTranslationCoordinator(AudioInputCoordinator &audio, AsrCoordinator &asr,
    TranslationCoordinator &translation, QObject *parent)
    : QObject(parent), audio_(audio), asr_(asr), translation_(translation)
{
    connect(&boundary_, &QTimer::timeout, this, &AudioTranslationCoordinator::finalizeBoundary);
    dispatchTimer_.setInterval(20);
    connect(&dispatchTimer_, &QTimer::timeout, this, [this] {
        if (running_ && configuration_.segmentation == Segmentation::Energy) {
            if (auto utterance = detector_.advanceTime(Audio::monotonicUs())) consumeEndpoint(std::move(*utterance));
        }
        dispatch();
    });
    connect(&audio_, &AudioInputCoordinator::pcmReady, this, &AudioTranslationCoordinator::receivePcm);
    connect(&audio_, &AudioInputCoordinator::stateChanged, this, [this](Audio::State state) {
        if (!running_) return;
        if (state == Audio::State::Running && configuration_.segmentation == Segmentation::LegacyFixed)
            boundary_.start(configuration_.segmentMs);
        else if (state == Audio::State::Stopped) {
            stop(); emit feedback(QStringLiteral("Audio capture stopped or device unavailable."));
        }
    });
    connect(&audio_, &AudioInputCoordinator::errorOccurred, this, [this](const Audio::Error &error) {
        stop(); emit feedback(error.message);
    });
    connect(&asr_, &AsrCoordinator::resultReady, this, &AudioTranslationCoordinator::receiveAsr);
    connect(&asr_, &AsrCoordinator::errorOccurred, this, &AudioTranslationCoordinator::receiveError);
    connect(&asr_, &AsrCoordinator::stateChanged, this, [this](Asr::State state) {
        if (running_ && (state == Asr::State::Unloaded || state == Asr::State::Loading)) {
            stop(); emit feedback(QStringLiteral("ASR model changed; audio pipeline stopped."));
        }
    });
    connect(&translation_, &TranslationCoordinator::requestStarted, this, [this](const TranslationRequest &request) {
        if (!running_ || !submitting_) return;
        requestId_ = request.requestId;
        emit translationRequested(requestSession_, requestUtterance_, request);
    });
    connect(&translation_, &TranslationCoordinator::resultReady, this, [this](const TranslationResult &result) {
        if (!running_ || requestSession_ != session_ || result.requestId != requestId_
            || requestUtterance_ != displayed_) return;
        requestId_ = 0;
        const auto generation = session_;
        const auto utterance = requestUtterance_;
        emit latencyMeasured(generation, utterance, QStringLiteral("translation"), requestSpeechEndUs_,
                             requestBoundaryUs_, Audio::monotonicUs(), result.elapsedMs);
        if (!running_ || session_ != generation || displayed_ != utterance) return;
        emit translationFinished(generation, requestUtterance_, result,
                                 (Audio::monotonicUs() - requestBoundaryUs_) / 1000);
        if (!running_ || session_ != generation || displayed_ != utterance) return;
        if (result.success) emit translatedTextReady(result.translatedText);
        else emit feedback(QStringLiteral("Translation failed: %1").arg(result.error));
    });
}
AudioTranslationCoordinator::~AudioTranslationCoordinator() { stop(); }
bool AudioTranslationCoordinator::start(const Configuration &configuration)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (running_) return false;
    if (asr_.state() != Asr::State::Ready || !configuration.endpoint.valid()
        || (configuration.segmentation != Segmentation::Energy && configuration.segmentation != Segmentation::LegacyFixed)
        || configuration.segmentMs < 100
        || configuration.segmentMs > 30000 || !Asr::supportedLanguage(configuration.asr.language)
        || configuration.asr.timeoutMs <= 0 || configuration.asr.threads < 1 || configuration.asr.threads > 64
        || !Asr::supportedLanguage(configuration.translationSource)
        || configuration.translationTarget == QLatin1String("auto")
        || !Asr::supportedLanguage(configuration.translationTarget)) {
        emit feedback(QStringLiteral("Load an ASR model and supply valid pipeline configuration before Start."));
        return false;
    }
    configuration_ = configuration; ++session_; current_.clear(); pending_.reset(); active_.reset();
    detector_.configure(configuration.endpoint); legacySpeechEndUs_ = 0;
    emit endpointEvent(QStringLiteral("detector_reset"));
    displayed_ = requestId_ = dropped_ = 0;
    translation_.invalidate(false);
    running_ = true;
    if (!audio_.start(configuration.kind, configuration.deviceId)) { stop(); return false; }
    audioSession_ = audio_.session();
    if (!asr_.beginUtterance(configuration.asr)) { stop(); return false; }
    prepared_ = true;
    asrSession_ = asr_.session(); asrUtterance_ = asr_.utterance();
    dispatchTimer_.start(); emit runningChanged(true);
    return true;
}
void AudioTranslationCoordinator::stop()
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (!running_) return;
    running_ = false; ++session_; boundary_.stop(); dispatchTimer_.stop();
    audio_.stop(); asr_.cancelUtterance(); current_.clear(); pending_.reset(); active_.reset();
    detector_.reset(); legacySpeechEndUs_ = requestSpeechEndUs_ = 0;
    requestId_ = 0; submitting_ = prepared_ = false; translation_.invalidate(false);
    emit runningChanged(false);
    emit endpointEvent(QStringLiteral("detector_reset"));
}
void AudioTranslationCoordinator::receivePcm(const Audio::PcmChunk &chunk)
{
    if (!running_ || chunk.session != audioSession_) return;
    const auto generation = session_;
    if (chunk.samples.size() != Audio::ChunkBytes) { emit feedback(QStringLiteral("Invalid PCM chunk rejected.")); return; }
    auto observation = detector_.consume(chunk, Audio::monotonicUs());
    emit endpointDetails(observation.rms, observation.noiseFloor, observation.startThreshold, int(observation.state));
    if (!running_ || session_ != generation) return;
    if (observation.reset) emit endpointEvent(QStringLiteral("detector_reset"));
    if (observation.started) emit endpointEvent(QStringLiteral("SpeechStarted"));
    if (!running_ || session_ != generation) return;
    if (configuration_.segmentation == Segmentation::Energy) {
        if (observation.ended) consumeEndpoint(std::move(*observation.ended));
        return;
    }
    // Legacy QA uses the same energy rule only to estimate last-active time, not to gate ASR.
    if (observation.rms >= observation.startThreshold * configuration_.endpoint.endRatio)
        legacySpeechEndUs_ = observation.chunkEndUs;
    // Cap a stalled event-loop window at 30 seconds without silently extending an utterance.
    if (current_.size() >= 1500) finalizeBoundary();
    if (running_ && generation == session_) current_.append(chunk);
}
void AudioTranslationCoordinator::finalizeBoundary()
{
    if (!running_ || configuration_.segmentation != Segmentation::LegacyFixed || current_.isEmpty()) return;
    auto chunks = std::move(current_); current_.clear();
    const auto speechEnd = legacySpeechEndUs_; legacySpeechEndUs_ = 0;
    seal(std::move(chunks), speechEnd);
}
void AudioTranslationCoordinator::consumeEndpoint(SpeechEndpointDetector::Utterance utterance)
{
    const auto generation = session_;
    emit endpointEvent(utterance.forced ? QStringLiteral("ForcedMaxDuration") : QStringLiteral("SpeechEnded"));
    if (running_ && session_ == generation) seal(std::move(utterance.chunks), utterance.speechEndUs);
}
void AudioTranslationCoordinator::seal(QList<Audio::PcmChunk> chunks, qint64 speechEndUs)
{
    if (!running_ || chunks.isEmpty()) return;
    const auto generation = session_;
    Segment segment{++nextUtterance_, std::move(chunks), Audio::monotonicUs(), speechEndUs};
    if (pending_) { ++dropped_; emit feedback(QStringLiteral("ASR backpressure: oldest pending segment dropped.")); }
    if (!running_ || session_ != generation) return;
    pending_ = std::move(segment);
    emit latencyMeasured(generation, pending_->id, QStringLiteral("boundary"), speechEndUs,
                         pending_->boundaryUs, pending_->boundaryUs, 0);
    if (!running_ || session_ != generation || !pending_) return;
    emit segmentFinalized(generation, pending_->id, pending_->chunks.size() * 20);
    if (running_ && session_ == generation) dispatch();
}
void AudioTranslationCoordinator::dispatch()
{
    if (!running_ || active_ || !pending_ || asr_.isBusy()) return;
    active_ = std::move(pending_); pending_.reset();
    if (!prepared_ && !asr_.beginUtterance(configuration_.asr)) { active_.reset(); return; }
    prepared_ = false;
    asrSession_ = asr_.session(); asrUtterance_ = asr_.utterance();
    // Chunk forwarding is owner-thread, bounded, and never runs inference here.
    const auto chunks = std::move(active_->chunks);
    for (const auto &chunk : chunks) {
        if (!running_ || !active_) return;
        if (!asr_.pushPcm(chunk)) { asr_.cancelUtterance(); active_.reset(); return; }
    }
    if (running_ && active_ && !asr_.finalizeUtterance()) active_.reset();
}
void AudioTranslationCoordinator::receiveAsr(const Asr::Result &result)
{
    if (!running_ || !active_ || result.session != asrSession_ || result.utterance != asrUtterance_
        || result.kind != Asr::ResultKind::Final) return;
    const auto segment = std::move(*active_); active_.reset();
    const auto generation = session_;
    emit latencyMeasured(generation, segment.id, QStringLiteral("asr_final"), segment.speechEndUs,
                         segment.boundaryUs, Audio::monotonicUs(), result.processingMs);
    if (!running_ || session_ != generation) return;
    emit finalReady(generation, segment.id, result);
    if (!running_ || session_ != generation) return;
    if (!result.text.trimmed().isEmpty()) {
        displayed_ = segment.id;
        requestId_ = 0; translation_.invalidate(false);
        emit originalTextReady(result.text);
        if (!running_ || session_ != generation) return;
        requestSession_ = generation; requestUtterance_ = segment.id; requestBoundaryUs_ = segment.boundaryUs;
        requestSpeechEndUs_ = segment.speechEndUs;
        submitting_ = true;
        translation_.acceptText(result.text, configuration_.translationSource, configuration_.translationTarget);
        submitting_ = false;
    }
    dispatch();
}
void AudioTranslationCoordinator::receiveError(const Asr::Error &error)
{
    if (!running_) return;
    const bool fatal = error.code == Asr::ErrorCode::ModelNotLoaded || error.code == Asr::ErrorCode::ModelLoadFailed;
    active_.reset();
    if (fatal) stop();
    emit feedback(QStringLiteral("ASR: %1").arg(error.message));
}
