#pragma once
#include "audio/AudioInputTypes.h"
#include <deque>
#include <optional>

struct SpeechEndpointConfig {
    int preRollMs = 300;
    int startMs = 100;
    int minimumSpeechMs = 250;
    int trailingSilenceMs = 600;
    int maxUtteranceMs = 12000;
    double minimumRms = 0.002;
    double minimumNoiseFloor = 0.00001;
    double noiseRatio = 3.0;
    double endRatio = 0.6;
    double maximumThreshold = 0.08;
    double noiseAlpha = 0.01;
    bool valid() const;
};

// Energy boundaries only: no capture, inference, translation or PCM alteration.
class SpeechEndpointDetector {
public:
    enum class State { Idle, MaybeSpeech, Speech, TrailingSilence };
    struct Utterance {
        QList<Audio::PcmChunk> chunks;
        qint64 speechEndUs = 0;
        bool forced = false;
    };
    struct Observation {
        double rms = 0, peak = 0, noiseFloor = 0, startThreshold = 0;
        State state = State::Idle;
        qint64 chunkEndUs = 0;
        bool started = false, reset = false;
        std::optional<Utterance> ended;
    };
    bool configure(const SpeechEndpointConfig &config);
    void reset();
    Observation consume(const Audio::PcmChunk &chunk, qint64 receivedUs = 0);
    // No synthetic padding when a loopback device stops delivering packets.
    std::optional<Utterance> advanceTime(qint64 nowUs);
    State state() const { return state_; }
    int bufferedChunks() const { return int(preRoll_.size() + current_.size()); }
    qint64 lastActiveUs() const { return lastActiveUs_; }
private:
    std::optional<Utterance> seal(bool forced);
    void retain(const Audio::PcmChunk &chunk);
    SpeechEndpointConfig config_;
    State state_ = State::Idle;
    std::deque<Audio::PcmChunk> preRoll_;
    QList<Audio::PcmChunk> current_;
    quint64 session_ = 0, sequence_ = 0;
    bool haveChunk_ = false;
    int activeMs_ = 0, candidateMs_ = 0, silenceMs_ = 0;
    qint64 lastActiveUs_ = 0, lastChunkEndUs_ = 0;
    qint64 lastReceivedUs_ = 0, clockOffsetUs_ = 0;
    double noiseFloor_ = 0.00001;
};
