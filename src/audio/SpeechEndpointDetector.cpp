#include "audio/SpeechEndpointDetector.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>

bool SpeechEndpointConfig::valid() const
{
    return preRollMs >= 0 && preRollMs <= 1000 && startMs >= 20 && startMs <= 500
        && minimumSpeechMs >= startMs && minimumSpeechMs <= 1000
        && trailingSilenceMs >= 20 && trailingSilenceMs <= 3000
        && maxUtteranceMs >= 1000 && maxUtteranceMs <= 15000
        && preRollMs + startMs + trailingSilenceMs < maxUtteranceMs
        && minimumSpeechMs + trailingSilenceMs < maxUtteranceMs
        && std::isfinite(minimumRms) && minimumRms > 0
        && std::isfinite(minimumNoiseFloor) && minimumNoiseFloor > 0 && minimumNoiseFloor <= minimumRms
        && std::isfinite(noiseRatio) && noiseRatio > 1 && noiseRatio <= 20
        && std::isfinite(endRatio) && endRatio > 0 && endRatio < 1
        && std::isfinite(maximumThreshold) && maximumThreshold >= minimumRms && maximumThreshold <= 1
        && std::isfinite(noiseAlpha) && noiseAlpha > 0 && noiseAlpha <= 0.05;
}
bool SpeechEndpointDetector::configure(const SpeechEndpointConfig &config)
{
    if (!config.valid()) return false;
    config_ = config; reset(); return true;
}
void SpeechEndpointDetector::reset()
{
    state_ = State::Idle; preRoll_.clear(); current_.clear(); haveChunk_ = false;
    activeMs_ = candidateMs_ = silenceMs_ = 0; lastActiveUs_ = lastChunkEndUs_ = 0;
    lastReceivedUs_ = clockOffsetUs_ = 0;
    noiseFloor_ = config_.minimumNoiseFloor;
}
void SpeechEndpointDetector::retain(const Audio::PcmChunk &chunk)
{
    preRoll_.push_back(chunk);
    while (int(preRoll_.size()) * 20 > config_.preRollMs) preRoll_.pop_front();
}
std::optional<SpeechEndpointDetector::Utterance> SpeechEndpointDetector::seal(bool forced)
{
    std::optional<Utterance> result;
    if (activeMs_ >= config_.minimumSpeechMs)
        result = Utterance{std::move(current_), lastActiveUs_, forced};
    current_.clear(); preRoll_.clear(); silenceMs_ = candidateMs_ = activeMs_ = 0;
    // Continuous speech proceeds directly into the next bounded utterance.
    state_ = forced ? State::Speech : State::Idle;
    return result;
}
SpeechEndpointDetector::Observation SpeechEndpointDetector::consume(const Audio::PcmChunk &chunk, qint64 receivedUs)
{
    Observation out;
    if (chunk.samples.size() != Audio::ChunkBytes || chunk.timestampUs < 0) return out;
    if (haveChunk_ && (chunk.session != session_ || chunk.sequence <= sequence_
        || chunk.timestampUs + 20000 <= lastChunkEndUs_)) { reset(); out.reset = true; }
    // WASAPI may deliver no packets during pause: its sample clock then excludes wall-clock silence.
    // Re-anchor only at first delivery or a packet gap, leaving PCM timestamps/bytes untouched.
    if (receivedUs > 0 && (!haveChunk_ || receivedUs - lastReceivedUs_ > 200000))
        clockOffsetUs_ = std::max(clockOffsetUs_, receivedUs - chunk.timestampUs - 20000);
    lastReceivedUs_ = receivedUs;
    haveChunk_ = true; session_ = chunk.session; sequence_ = chunk.sequence;
    lastChunkEndUs_ = chunk.timestampUs + 20000;
    out.chunkEndUs = lastChunkEndUs_ + clockOffsetUs_;
    if (receivedUs > 0) out.chunkEndUs = std::min(out.chunkEndUs, receivedUs);
    double sum = 0;
    for (int i = 0; i < Audio::ChunkSamples; ++i) {
        const double sample = qFromLittleEndian<qint16>(chunk.samples.constData() + i * 2) / 32768.0;
        sum += sample * sample; out.peak = std::max(out.peak, std::abs(sample));
    }
    out.rms = std::sqrt(sum / Audio::ChunkSamples);
    out.startThreshold = std::min(config_.maximumThreshold, std::max(config_.minimumRms, noiseFloor_ * config_.noiseRatio));
    const bool startActive = out.rms >= out.startThreshold;
    const bool holdActive = out.rms >= out.startThreshold * config_.endRatio;
    if (state_ == State::Idle) {
        if (startActive) { state_ = State::MaybeSpeech; candidateMs_ = 20; current_.append(chunk); }
        else {
            // Idle-only slow EMA. Clip upward innovations so a sudden voice cannot raise the floor.
            const double observed = std::min(out.rms, out.startThreshold * config_.endRatio);
            noiseFloor_ = std::max(config_.minimumNoiseFloor, noiseFloor_ + config_.noiseAlpha * (observed - noiseFloor_));
            retain(chunk);
        }
    } else if (state_ == State::MaybeSpeech) {
        if (startActive) { current_.append(chunk); candidateMs_ += 20; }
        else {
            for (const auto &candidate : current_) retain(candidate);
            current_.clear(); candidateMs_ = 0; state_ = State::Idle; retain(chunk);
        }
    } else {
        current_.append(chunk);
        if (holdActive) { activeMs_ += 20; silenceMs_ = 0; state_ = State::Speech; lastActiveUs_ = out.chunkEndUs; }
        else { silenceMs_ += 20; state_ = State::TrailingSilence; }
    }
    if (state_ == State::MaybeSpeech && candidateMs_ >= config_.startMs) {
        QList<Audio::PcmChunk> prefix;
        prefix.reserve(int(preRoll_.size()) + current_.size());
        for (const auto &prior : preRoll_) prefix.append(prior);
        prefix.append(current_); current_ = std::move(prefix); preRoll_.clear();
        activeMs_ = candidateMs_; lastActiveUs_ = out.chunkEndUs; state_ = State::Speech; out.started = true;
    }
    if ((state_ == State::Speech || state_ == State::TrailingSilence) && current_.size() * 20 >= config_.maxUtteranceMs)
        out.ended = seal(true);
    else if (state_ == State::TrailingSilence && silenceMs_ >= config_.trailingSilenceMs)
        out.ended = seal(false);
    out.state = state_; out.noiseFloor = noiseFloor_;
    return out;
}
std::optional<SpeechEndpointDetector::Utterance> SpeechEndpointDetector::advanceTime(qint64 nowUs)
{
    if (haveChunk_ && nowUs >= std::max(lastChunkEndUs_ + clockOffsetUs_, lastReceivedUs_)
        + qint64(config_.trailingSilenceMs) * 1000
        && (state_ == State::Idle || state_ == State::MaybeSpeech || current_.isEmpty())) {
        preRoll_.clear(); current_.clear(); state_ = State::Idle; candidateMs_ = activeMs_ = silenceMs_ = 0;
    }
    if ((state_ == State::Speech || state_ == State::TrailingSilence) && !current_.isEmpty()
        && nowUs >= std::max(lastChunkEndUs_ + clockOffsetUs_, lastReceivedUs_) + 40000
        && nowUs >= lastActiveUs_ + qint64(config_.trailingSilenceMs) * 1000)
        return seal(false);
    return std::nullopt;
}
