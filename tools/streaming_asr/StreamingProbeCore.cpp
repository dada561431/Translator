#include "StreamingProbeCore.h"
#include "asr/AsrAudioBuffer.h"
#include "audio/AudioInputTypes.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <algorithm>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#endif

namespace StreamingProbe {
namespace {
int prefixLength(const QString &a, const QString &b) {
    int n = 0;
    while (n < a.size() && n < b.size() && a[n] == b[n]) ++n;
    // Do not split a UTF-16 surrogate pair.
    if (n && a[n - 1].isHighSurrogate()) --n;
    return n;
}
int characters(const QString &text) { return int(text.toUcs4().size()); }
}
TextChange TextStabilizer::update(const QString &text) {
    const int common = prefixLength(previous_, text);
    const auto candidate = text.left(common);
    const bool conflict = !text.startsWith(stable_);
    // Agreement is only a hypothesis. A later rewrite across it is reported,
    // never concealed by changing the raw ASR text or rolling stable back.
    if (!conflict && candidate.startsWith(stable_)) stable_ = candidate;
    TextChange result{previous_, text, stable_, conflict ? text : text.mid(stable_.size()),
        characters(candidate), characters(text.mid(common)), characters(previous_.mid(common)), conflict};
    previous_ = text;
    return result;
}
bool PcmMailbox::push(QByteArray pcm, bool discontinuity) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return false;
    if (size_t(pcm.size()) > capacity_ - stats_.bytes) { ++stats_.dropped; return false; }
    stats_.bytes += size_t(pcm.size());
    stats_.highWaterBytes = std::max(stats_.bytes, stats_.highWaterBytes);
    chunks_.emplace_back(std::move(pcm), discontinuity); changed_.notify_one(); return true;
}
bool PcmMailbox::waitPop(QByteArray &pcm, bool *discontinuity) {
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait(lock, [&] { return closed_ || !chunks_.empty(); });
    if (chunks_.empty()) return false;
    if (discontinuity) *discontinuity = chunks_.front().second;
    pcm = std::move(chunks_.front().first); chunks_.pop_front(); stats_.bytes -= size_t(pcm.size());
    return true;
}
void PcmMailbox::close() {
    std::lock_guard<std::mutex> lock(mutex_); closed_ = true; changed_.notify_all();
}
QueueStats PcmMailbox::stats() const { std::lock_guard<std::mutex> lock(mutex_); return stats_; }
qint64 workingSetBytes() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS c{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof(c))) return qint64(c.WorkingSetSize);
#endif
    return -1;
}
double processCpuMs() {
#ifdef Q_OS_WIN
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        auto value = [](FILETIME f) { return double((quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 10000.; };
        return value(kernel) + value(user);
    }
#endif
    return -1;
}
Worker::Worker(Factory factory, Sink sink, bool endpoints, size_t capacity, int flushMs)
    : factory_(std::move(factory)), sink_(std::move(sink)), endpoints_(endpoints), capacity_(capacity),
      flushMs_(flushMs), mailbox_(capacity) {
    if (!capacity || flushMs < 0 || flushMs > 2000) throw std::invalid_argument("Invalid queue capacity/flush duration");
}
Worker::~Worker() { stop(); }
bool Worker::start() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (thread_.joinable() || stopping_) return false;
    thread_ = std::thread([this] { run(); });
    stateChanged_.wait(lock, [&] { return loaded_ || failed_; });
    return loaded_ && !failed_;
}
void Worker::beginAudio() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_ || failed_ || begun_ || stopping_) return;
    originUs_ = Audio::monotonicUs(); begun_ = true; stateChanged_.notify_all();
}
bool Worker::push(const QByteArray &pcm, bool discontinuity) {
    { std::lock_guard<std::mutex> lock(mutex_); if (!begun_ || failed_ || stopping_) return false; }
    if (pcm.isEmpty() || pcm.size() % 2 || pcm.size() > 6400) return false;
    return mailbox_.push(pcm, discontinuity);
}
bool Worker::failed() const { std::lock_guard<std::mutex> lock(mutex_); return failed_; }
void Worker::stop() {
    { std::lock_guard<std::mutex> lock(mutex_);
      if (!stopping_) stopRequestedUs_ = Audio::monotonicUs();
      stopping_ = true; stateChanged_.notify_all(); }
    mailbox_.close();
    if (thread_.joinable()) thread_.join();
}
void Worker::run() {
    try {
        sink_({{"event", "before_model"}, {"working_set_bytes", workingSetBytes()}});
        QElapsedTimer load; load.start();
        auto recognizer = factory_();
        if (!recognizer) throw std::runtime_error("Recognizer factory returned null");
        auto metadata = recognizer->load();
        metadata.insert("event", "model_loaded"); metadata.insert("load_ms", load.elapsed());
        metadata.insert("load_count", 1); metadata.insert("working_set_bytes", workingSetBytes()); sink_(metadata);
        qint64 origin = 0;
        { std::unique_lock<std::mutex> lock(mutex_); loaded_ = true; stateChanged_.notify_all();
          stateChanged_.wait(lock, [&] { return begun_ || stopping_; });
          if (!begun_) return;
          origin = originUs_; }
        const double cpuStart = processCpuMs();
        qint64 received = 0, revision = 0, segment = 0, decodeCount = 0;
        qint64 first = -1, last = -1, rewrites = 0, conflicts = 0;
        qint64 lastProgressSamples = 0;
        qint64 maxMemory = workingSetBytes();
        double computeMs = 0;
        bool flushing = false, discontinuity = false;
        int discontinuities = 0;
        QJsonArray intervals;
        QString previous;
        TextStabilizer stabilizer;
        auto wall = [&] { return (Audio::monotonicUs() - origin) / 1000.; };
        auto emitText = [&] {
            const QString text = recognizer->text();
            if (text == previous) return;
            const auto change = stabilizer.update(text);
            previous = text;
            const double now = wall();
            if (!text.isEmpty() && first < 0) first = qint64(now);
            if (last >= 0) intervals.append(now - last);
            last = qint64(now);
            if (change.removed) ++rewrites;
            if (change.stableConflict) ++conflicts;
            const auto q = mailbox_.stats();
            sink_({{"event", "partial"}, {"wall_ms", now}, {"time_ms", now},
                {"audio_received_ms", received / 16.}, {"audio_ms", received / 16.},
                {"revision", ++revision}, {"segment", segment}, {"text", text},
                {"during_flush", flushing},
                {"text_length", characters(text)}, {"previous", change.previous},
                {"common_prefix_length", change.commonPrefix}, {"added_chars", change.added},
                {"removed_replaced_chars", change.removed}, {"stable", change.stable},
                {"unstable", change.unstable}, {"stable_conflict", change.stableConflict},
                {"backlog_ms", q.bytes / 32.}, {"dropped_chunks", qint64(q.dropped)}});
        };
        auto decode = [&] {
            while (recognizer->ready()) {
                QElapsedTimer timer; timer.start(); recognizer->decode();
                computeMs += timer.nsecsElapsed() / 1e6; ++decodeCount; emitText();
            }
        };
        auto finalize = [&](const char *reason, double at) {
            QJsonObject event{{"event", "final"}, {"reason", reason}, {"segment", segment},
                {"wall_ms", wall()}, {"audio_ms", received / 16.}, {"text", recognizer->text()},
                {"endpoint_to_final_ms", wall() - at}, {"revision", revision}};
            if (QByteArray(reason) == "input_finished") {
                std::lock_guard<std::mutex> lock(mutex_);
                event.insert("stop_request_to_final_ms", (Audio::monotonicUs() - stopRequestedUs_) / 1000.);
            }
            sink_(event);
        };
        QByteArray pcm;
        while (mailbox_.waitPop(pcm, &discontinuity)) {
            if (discontinuity) {
                ++discontinuities; recognizer->reset(); ++segment; previous.clear(); stabilizer.reset();
                sink_({{"event", "stream_reset"}, {"reason", "capture_discontinuity"},
                    {"audio_ms", received / 16.}, {"wall_ms", wall()}, {"segment", segment}});
            }
            auto samples = AsrAudioBuffer::toFloat(pcm); received += qint64(samples.size());
            QElapsedTimer timer; timer.start(); recognizer->accept(samples);
            computeMs += timer.nsecsElapsed() / 1e6; decode();
            maxMemory = std::max(maxMemory, workingSetBytes());
            if (received - lastProgressSamples >= 3200) {
                lastProgressSamples = received;
                sink_({{"event", "progress"}, {"wall_ms", wall()}, {"audio_ms", received / 16.},
                    {"compute_ms", computeMs}, {"decode_count", decodeCount},
                    {"ready_drained", !recognizer->ready()}, {"backlog_ms", mailbox_.stats().bytes / 32.}});
            }
            if (endpoints_ && recognizer->endpoint()) {
                const double at = wall();
                sink_({{"event", "endpoint"}, {"wall_ms", at}, {"audio_ms", received / 16.}, {"segment", segment}});
                finalize("sherpa_endpoint", at); recognizer->reset();
                ++segment; previous.clear(); stabilizer.reset();
            }
        }
        const double stopAt = wall();
        // Upstream example uses 0.3 s; optional QA tail A/B is explicit in metrics.
        // Synthetic tail is not counted as real input audio or streaming evidence.
        flushing = true;
        QElapsedTimer tail; tail.start(); recognizer->accept(std::vector<float>(size_t(flushMs_ * 16), 0.f)); recognizer->finish();
        computeMs += tail.nsecsElapsed() / 1e6; decode(); finalize("input_finished", stopAt);
        const auto q = mailbox_.stats();
        const double cpuMs = processCpuMs() - cpuStart;
        sink_({{"event", "summary"}, {"wall_ms", wall()}, {"audio_ms", received / 16.},
            {"first_nonempty_partial_ms", first}, {"partial_update_intervals_ms", intervals},
            {"revision_count", revision}, {"rewrite_count", rewrites}, {"stable_conflict_count", conflicts},
            {"decode_count", decodeCount}, {"compute_ms", computeMs},
            {"rtf", received ? computeMs / (received / 16.) : 0.}, {"process_cpu_ms", cpuMs},
            {"cpu_single_core_percent", wall() > 0 ? cpuMs / wall() * 100 : 0},
            {"max_working_set_bytes", maxMemory}, {"queue_capacity_bytes", qint64(capacity_)},
            {"queue_high_water_ms", q.highWaterBytes / 32.}, {"backlog_ms", q.bytes / 32.},
            {"dropped_chunks", qint64(q.dropped)}, {"model_load_count", 1},
            {"flush_zero_tail_ms", flushMs_}, {"capture_discontinuities", discontinuities}, {"translation_requests", 0}});
        recognizer.reset();
        sink_({{"event", "model_unloaded"}, {"working_set_bytes", workingSetBytes()}});
    } catch (const std::exception &e) {
        { std::lock_guard<std::mutex> lock(mutex_); failed_ = true; stateChanged_.notify_all(); }
        sink_({{"event", "error"}, {"message", QString::fromUtf8(e.what())}});
    }
}
}
