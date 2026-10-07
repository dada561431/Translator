"""Explicit local QA: build paced corpus or evaluate existing Probe JSONL. No network."""
import argparse
import array
import hashlib
import json
import math
import statistics
import sys
import wave
from pathlib import Path

from analyze_streaming_asr_probe import edit_distance


def normalize(text, language):
    import unicodedata
    text = "".join(c for c in text.lower() if not unicodedata.category(c).startswith("P"))
    text = " ".join(text.split())
    return list("".join(text.split())) if language == "zh" else text.split()


def read_pcm(path):
    with wave.open(str(path), "rb") as audio:
        if (audio.getframerate(), audio.getnchannels(), audio.getsampwidth()) != (16000, 1, 2):
            raise ValueError("Require unified 16k mono PCM16 WAV")
        return audio.readframes(audio.getnframes())


def active_span(pcm, threshold=0.0005):
    """Waveform-assisted 5 ms RMS boundaries, NOT ASR-derived timestamps/VAD."""
    samples = array.array("h", pcm)
    if sys.byteorder != "little":
        samples.byteswap()
    active = []
    for offset in range(0, len(samples), 80):
        frame = samples[offset:offset + 80]
        rms = math.sqrt(sum((s / 32768) ** 2 for s in frame) / len(frame))
        if rms > threshold:
            active.append(offset)
    if not active:
        raise ValueError("No waveform activity: manual annotation required")
    return active[0] / 16, min(active[-1] + 80, len(samples)) / 16


def write_new(path, data):
    with path.open("x", encoding="utf-8") as output:
        json.dump(data, output, ensure_ascii=False, indent=2)


def prepare(corpus, directory, output):
    entries, chunks, position = [], [], 0
    for item in corpus:
        pcm = read_pcm(directory / (item["id"] + ".wav"))
        start, end = active_span(pcm)
        silence = bytes(32000)  # one second pre-roll per utterance
        clip = silence + pcm + bytes(32000 * 3)
        entry = dict(item, speech_start_ms=position + 1000 + start,
                     speech_end_ms=position + 1000 + end, window_start_ms=position,
                     window_end_ms=position + len(clip) / 32,
                     span_provenance="waveform_5ms_rms_0.0005_assisted",
                     source="Windows offline TTS; not human accuracy evidence",
                     source_pcm_sha256=hashlib.sha256(pcm).hexdigest())
        entries.append(entry)
        chunks.append(clip)
        position = entry["window_end_ms"]
    if output.exists() or output.with_suffix(".manifest.json").exists():
        raise ValueError("Refusing to overwrite QA input")
    with wave.open(str(output), "wb") as audio:
        audio.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
        audio.writeframes(b"".join(chunks))
    write_new(output.with_suffix(".manifest.json"), entries)


def annotate_recording(corpus, recorded, output, source, first_ms=3000, interval_ms=15000,
                       windows=None, threshold=0.002):
    pcm = read_pcm(recorded)
    entries = []
    previous_end = 0
    if windows is not None and len(windows) != len(corpus):
        raise ValueError("Explicit window count must match references")
    for index, item in enumerate(corpus):
        window = windows[index] if windows is not None else {}
        if windows is not None and window["id"] != item["id"]:
            raise ValueError("Explicit windows must match reference order")
        low = window.get("window_start_ms", first_ms + index * interval_ms)
        high = window.get("window_end_ms", min(first_ms + (index + 1) * interval_ms, len(pcm) / 32))
        if low < previous_end or high <= low or high > len(pcm) / 32:
            raise ValueError("Invalid, overlapping or out-of-recording windows")
        previous_end = high
        section = pcm[int(low * 32):int(high * 32)]
        start, end = active_span(section, threshold=threshold)
        speech_start = window.get("speech_start_ms", low + start)
        speech_end = window.get("speech_end_ms", low + end)
        if not low <= speech_start < speech_end <= high:
            raise ValueError("Speech boundaries must be inside the independent window")
        entries.append(dict(item, window_start_ms=low, window_end_ms=high,
                            speech_start_ms=speech_start, speech_end_ms=speech_end,
                            span_provenance=f"waveform_5ms_rms_{threshold}_assisted; explicit windows/boundaries are independent of ASR",
                            source=source, ground_truth_status=window.get("ground_truth_status", "OWNER READ-CONFIRMATION REQUIRED"),
                            source_pcm_sha256=hashlib.sha256(section).hexdigest()))
    write_new(output, entries)


def annotate_loopback(manifest, recorded, events, output):
    starts = [e for e in events if e.get("event") == "playback_start"]
    if len(starts) != 1:
        raise ValueError("Require exactly one fixed-corpus playback event")
    pcm = read_pcm(recorded)
    offset = starts[0]["wall_ms"]
    entries = []
    for original in manifest:
        low, high = original["window_start_ms"] + offset, original["window_end_ms"] + offset
        section = pcm[int(low * 32):int(high * 32)]
        start, end = active_span(section)
        # Fixed corpus has 1s pre-roll and 3s post-roll. Continuous background
        # activity makes independent speech-boundary annotation invalid.
        if start < 200 or end > len(section) / 32 - 300:
            raise ValueError("Loopback lacks silent margins; verify isolation before assigning speech boundaries")
        entries.append(dict(original, window_start_ms=low, window_end_ms=high,
                            speech_start_ms=low + start, speech_end_ms=low + end,
                            span_provenance="captured_loopback_5ms_rms_0.0005_assisted",
                            source="Windows WASAPI capture during offline TTS playback; microphone not opened",
                            isolation_status="OWNER ISOLATION CONFIRMATION AND TRACE REVIEW REQUIRED",
                            source_pcm_sha256=hashlib.sha256(section).hexdigest()))
    write_new(output, entries)


def utterance(events, entry):
    low, high = entry["window_start_ms"], entry["window_end_ms"]
    partials = [e for e in events if e.get("event") == "partial" and e.get("text")
                and not e.get("during_flush") and low <= e.get("audio_ms", -1) < high]
    finals = [e for e in events if e.get("event") == "final" and e.get("text")
              and low <= e.get("audio_ms", -1) < high]
    endpoints = [e for e in events if e.get("event") == "endpoint"
                 and low <= e.get("audio_ms", -1) < high and finals
                 and e.get("segment") == finals[-1].get("segment")]
    final = " ".join(e["text"] for e in finals)
    ref, hyp = normalize(entry["reference"], entry["language"]), normalize(final, entry["language"])
    intervals = [b["wall_ms"] - a["wall_ms"] for a, b in zip(partials, partials[1:])
                 if a.get("segment") == b.get("segment")]
    stable = next((e for e in partials if e.get("stable")), None)
    rewritten = sum(e.get("removed_replaced_chars", 0) for e in partials)
    emitted = sum(e.get("added_chars", len(e["text"])) for e in partials)
    progress = [e for e in events if e.get("event") == "progress"]
    before = next((e for e in reversed(progress) if e["audio_ms"] <= low), {"compute_ms": 0})
    after = next((e for e in reversed(progress) if e["audio_ms"] < high), None)
    compute = after["compute_ms"] - before["compute_ms"] if after else None
    at = lambda e, boundary: e["wall_ms"] - boundary if e else None
    return dict(entry, final=final, **{"cer" if entry["language"] == "zh" else "wer":
        edit_distance(ref, hyp) / len(ref) if ref else None},
        first_partial_ms=at(partials[0] if partials else None, entry["speech_start_ms"]),
        audio_start_to_first_partial_ms=partials[0]["wall_ms"] if partials else None,
        first_stable_ms=at(stable, entry["speech_start_ms"]),
        speech_end_to_endpoint_ms=at(endpoints[-1] if endpoints else None, entry["speech_end_ms"]),
        speech_end_to_final_ms=at(finals[-1] if finals else None, entry["speech_end_ms"]),
        partial_count=len(partials), revision_count=len(partials),
        before_end_count=sum(e["wall_ms"] < entry["speech_end_ms"] for e in partials),
        median_update_ms=statistics.median(intervals) if intervals else None,
        compute_ms=compute, rtf=compute / (high - low) if compute is not None else None,
        rewrite_count=sum(bool(e.get("removed_replaced_chars")) for e in partials),
        rewrite_chars=rewritten, emitted_chars=emitted, rewrite_rate=rewritten / emitted if emitted else 0,
        first_partial=partials[0] if partials else None,
        middle_partial=partials[len(partials) // 2] if partials else None,
        last_partial=partials[-1] if partials else None,
        stable_prefix_growth=[{"wall_ms": e["wall_ms"], "characters": len(e.get("stable", "")),
                               "common_prefix_length": e.get("common_prefix_length"),
                               "stable_conflict": e.get("stable_conflict", False)} for e in partials],
        semantic_acceptable=None, semantic_note="Requires independent human judgment, never inferred from CER/WER")


def evaluate(events, manifest):
    loaded = next((e for e in events if e.get("event") == "model_loaded"), {})
    rows = [dict(utterance(events, entry), model=loaded.get("model")) for entry in manifest]
    summary = next((e for e in events if e.get("event") == "summary"), {})
    stats = {}
    for language, metric in (("en", "wer"), ("zh", "cer")):
        scores = [r[metric] for r in rows if r["language"] == language and r[metric] is not None]
        stats[language] = {"count": len(scores), "mean": statistics.mean(scores) if scores else None,
                           "median": statistics.median(scores) if scores else None}
    latencies = sorted(r["first_partial_ms"] for r in rows if r["first_partial_ms"] is not None)
    stats["first_partial_median_ms"] = statistics.median(latencies) if latencies else None
    stats["first_partial_p90_ms"] = latencies[math.ceil(.9 * len(latencies)) - 1] if len(latencies) >= 10 else None
    intervals = [r["median_update_ms"] for r in rows if r["median_update_ms"] is not None]
    stats["update_median_ms"] = statistics.median(intervals) if intervals else None
    emitted = sum(r["emitted_chars"] for r in rows)
    stats["rewrite_rate"] = sum(r["rewrite_chars"] for r in rows) / emitted if emitted else 0
    for field in ("first_stable_ms", "speech_end_to_endpoint_ms", "speech_end_to_final_ms"):
        values = [r[field] for r in rows if r[field] is not None]
        stats[field + "_median"] = statistics.median(values) if values else None
    samples = [e for e in events if e.get("event") == "queue"]
    cpu = [(b["process_cpu_ms"] - a["process_cpu_ms"]) / (b["wall_ms"] - a["wall_ms"]) * 100
           for a, b in zip(samples, samples[1:]) if b["wall_ms"] > a["wall_ms"]
           and "process_cpu_ms" in a and "process_cpu_ms" in b]
    stats["peak_cpu_single_core_percent"] = max(cpu) if cpu else None
    return {"schema_version": 1, "model": loaded.get("model"), "utterances": rows,
            "accuracy_summary": stats, "runtime_summary": summary, "load": loaded,
            "memory_events": [e for e in events if e.get("event") in ("before_model", "model_unloaded")],
            "teardown": next((e for e in reversed(events) if e.get("event") == "teardown"), {}),
            "errors": [e for e in events if e.get("event") in ("error", "capture_error", "playback_error")],
            "normalization": "lowercase, Unicode punctuation removal, whitespace collapse; zh removes spaces; no correction",
            "processing_progress_note": "audio_ms is worker-accepted PCM, not exact decoder/token alignment; queue and IsReady drain measure throughput"}


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("prepare", "evaluate", "annotate-recording", "annotate-loopback"))
    p.add_argument("--corpus", type=Path)
    p.add_argument("--wav-dir", type=Path)
    p.add_argument("--manifest", type=Path)
    p.add_argument("--log", type=Path)
    p.add_argument("--recorded", type=Path)
    p.add_argument("--source", default="explicit QA microphone")
    p.add_argument("--ids", help="Explicit comma-separated subset of the fixed corpus, in requested order")
    p.add_argument("--cue-interval-ms", type=int, default=15000)
    p.add_argument("--windows-json", type=Path, help="Independent reference/window JSON for repeated or additional QA speech")
    p.add_argument("--annotation-rms", type=float, default=0.002, help="Offline annotation only; never recognizer filtering")
    p.add_argument("--semantic-review", type=Path, help="Independent manual JSON {utterance_id: true/false}; never model-inferred")
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    corpus = json.loads(a.corpus.read_text(encoding="utf-8")) if a.corpus else None
    if a.ids:
        by_id = {e["id"]: e for e in corpus}
        corpus = [by_id[i] for i in a.ids.split(",")]
    if a.mode == "prepare":
        prepare(corpus, a.wav_dir, a.output)
    elif a.mode == "annotate-recording":
        windows = json.loads(a.windows_json.read_text(encoding="utf-8")) if a.windows_json else None
        if windows is not None:
            corpus = windows
        if not 0 < a.annotation_rms < 1:
            raise ValueError("Annotation RMS must be between zero and one")
        annotate_recording(corpus, a.recorded, a.output, a.source, interval_ms=a.cue_interval_ms,
                           windows=windows, threshold=a.annotation_rms)
    elif a.mode == "annotate-loopback":
        events = [json.loads(line) for line in a.log.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
        annotate_loopback(json.loads(a.manifest.read_text(encoding="utf-8")), a.recorded, events, a.output)
    else:
        events = [json.loads(line) for line in a.log.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
        result = evaluate(events, json.loads(a.manifest.read_text(encoding="utf-8")))
        if a.semantic_review:
            review = json.loads(a.semantic_review.read_text(encoding="utf-8"))
            for row in result["utterances"]:
                value = review.get(row["id"])
                if type(value) is not bool:
                    raise ValueError("Semantic review must independently judge every utterance true/false")
                row["semantic_acceptable"] = value
                row["semantic_note"] = "Caller-supplied manual reference/hypothesis judgment, separate from WER/CER"
        write_new(a.output, result)
        print(json.dumps(result["accuracy_summary"], ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
