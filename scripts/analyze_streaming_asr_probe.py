"""Local JSONL analysis only. No audio, model, network, or production imports."""
import argparse
import json
import statistics
import re
import unicodedata
import sys
from pathlib import Path


def edit_distance(left, right):
    previous = list(range(len(right) + 1))
    for i, a in enumerate(left, 1):
        row = [i]
        for j, b in enumerate(right, 1):
            row.append(min(row[-1] + 1, previous[j] + 1, previous[j - 1] + (a != b)))
        previous = row
    return previous[-1]


def normalized(text):
    text = "".join(c for c in text.lower() if not unicodedata.category(c).startswith(("P", "S")))
    return " ".join(text.split())


def analyze(events, speech_start_ms=None, speech_end_ms=None, ground_truth=None):
    partials = [e for e in events if e.get("event") == "partial"
                and e.get("text") and not e.get("during_flush", False)]
    first = partials[0]["wall_ms"] if partials else None
    intervals = [b["wall_ms"] - a["wall_ms"] for a, b in zip(partials, partials[1:])]
    groups = {}
    for e in partials:
        groups.setdefault(e.get("segment", 0), []).append(e)
    within = [b["wall_ms"] - a["wall_ms"] for g in groups.values()
              for a, b in zip(g, g[1:])]
    loaded = [e for e in events if e.get("event") == "model_loaded"]
    summary = next((e for e in reversed(events) if e.get("event") == "summary"), {})
    teardown = next((e for e in reversed(events) if e.get("event") == "teardown"), {})
    result = {
        "model_load_events": len(loaded),
        "model_load_ms": loaded[0].get("load_ms") if loaded else None,
        "audio_start_to_first_partial_ms": first,
        "speech_start_to_first_partial_ms": first - speech_start_ms
        if first is not None and speech_start_ms is not None else None,
        "partials_before_annotated_speech_end": sum(e["wall_ms"] < speech_end_ms for e in partials)
        if speech_end_ms is not None else None,
        "nonempty_streaming_partial_count": len(partials),
        "partial_interval_median_ms": statistics.median(intervals) if intervals else None,
        "within_segment_interval_median_ms": statistics.median(within) if within else None,
        "sequences": {k: [{"wall_ms": e["wall_ms"], "audio_ms": e["audio_ms"], "text": e["text"]}
                          for e in g] for k, g in groups.items()},
        "finals": [e for e in events if e.get("event") == "final" and e.get("text")],
        "summary": summary,
        "teardown": teardown,
        "maximum_sampled_queue_ms": max((e.get("backlog_ms", 0) for e in events), default=0),
        "translation_requests": summary.get("translation_requests"),
        "speech_span_note": "External manual annotations only; cue time and file duration are NOT speech boundaries.",
    }
    if ground_truth is not None:
        truth = normalized(ground_truth)
        final = normalized(" ".join(e["text"] for e in result["finals"]))
        truth_chars, final_chars = re.sub(r"\s", "", truth), re.sub(r"\s", "", final)
        result["accuracy"] = {
            "ground_truth": ground_truth, "final": final,
            "normalized_exact_match": truth == final,
            "cer": edit_distance(truth_chars, final_chars) / len(truth_chars) if truth_chars else None,
            "wer": edit_distance(truth.split(), final.split()) / len(truth.split()) if truth else None,
            "normalization": "lowercase, remove Unicode punctuation/symbols, collapse spaces; CER removes spaces. No linguistic correction.",
            "provenance": "Caller-supplied independent ground truth; analyzer does not infer ground truth.",
        }
    return result


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--speech-start-ms", type=float)
    parser.add_argument("--speech-end-ms", type=float)
    parser.add_argument("--ground-truth", help="Independent human/reference transcript, never inferred from ASR")
    args = parser.parse_args()
    with args.log.open(encoding="utf-8-sig") as stream:
        events = [json.loads(line) for line in stream if line.strip()]
    print(json.dumps(analyze(events, args.speech_start_ms, args.speech_end_ms, args.ground_truth), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
