#pragma once
#include "audio/AudioInputTypes.h"
#include <deque>
#include <mutex>

// Capture never waits on consumer work. A slow owner retains at most one second.
class AudioChunkBuffer
{
public:
    void push(Audio::PcmChunk chunk) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (chunks_.size() == 50) {
            chunks_.pop_front(); ++dropped_;
            if (!chunks_.empty()) chunks_.front().discontinuity = true;
            else chunk.discontinuity = true;
        }
        chunks_.push_back(std::move(chunk));
    }
    QList<Audio::PcmChunk> take(int maximum = 10) {
        std::lock_guard<std::mutex> lock(mutex_);
        QList<Audio::PcmChunk> result;
        while (maximum-- > 0 && !chunks_.empty()) {
            result.append(std::move(chunks_.front())); chunks_.pop_front();
        }
        return result;
    }
    quint64 dropped() const { std::lock_guard<std::mutex> lock(mutex_); return dropped_; }
    size_t size() const { std::lock_guard<std::mutex> lock(mutex_); return chunks_.size(); }
    void clear() { std::lock_guard<std::mutex> lock(mutex_); chunks_.clear(); dropped_ = 0; }
private:
    mutable std::mutex mutex_;
    std::deque<Audio::PcmChunk> chunks_;
    quint64 dropped_ = 0;
};
