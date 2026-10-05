"""Evaluation helpers; no OCR imports or network calls."""
import csv
import html
import json
import math
import re
import statistics
from pathlib import Path


def normalize(text):
    text = text.replace("\r\n", "\n").replace("\r", "\n").strip()
    return "\n".join(re.sub(r" +", " ", line.strip()) for line in text.split("\n"))


def edit_distance(a, b):
    previous = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        current = [i]
        for j, cb in enumerate(b, 1):
            current.append(min(current[-1] + 1, previous[j] + 1, previous[j - 1] + (ca != cb)))
        previous = current
    return previous[-1]


def load_manifest(path, image_root):
    path, root = Path(path), Path(image_root).resolve()
    if not path.is_file():
        raise ValueError("REAL VIDEO A/B BENCHMARK: BLOCKED - no user ground-truth sample set")
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if len(reader.fieldnames or []) != 4 or set(reader.fieldnames or []) != {"filename", "language", "ground_truth", "category"}:
            raise ValueError("manifest columns must be filename,language,ground_truth,category")
        samples, seen = [], set()
        for index, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f"Malformed CSV row {index}")
            filename = row["filename"].strip()
            image = (root / filename).resolve()
            if not filename or not image.is_relative_to(root) or Path(filename).is_absolute():
                raise ValueError(f"Unsafe filename at row {index}")
            if image in seen:
                raise ValueError(f"Duplicate image at row {index}")
            seen.add(image)
            if not image.is_file():
                raise ValueError(f"Missing image at row {index}: {filename}")
            if not normalize(row["ground_truth"]):
                raise ValueError(f"Empty ground truth at row {index}")
            if row["language"] not in {"zh", "en", "ja", "ko"}:
                raise ValueError(f"Unsupported language at row {index}")
            if not row["category"].strip():
                raise ValueError(f"Empty category at row {index}")
            samples.append(dict(row, filename=filename, path=str(image)))
        if not samples:
            raise ValueError("Manifest has no samples")
        return samples


def percentile95(values):
    return sorted(values)[max(0, math.ceil(len(values) * .95) - 1)] if values else None


def evaluate(sample, result, engine):
    error = result.get("error", "")
    raw = result.get("text", "")
    prediction, truth = normalize(raw), normalize(sample["ground_truth"])
    # Failed inference remains in the denominator, conservatively as an empty prediction.
    scored_prediction = "" if error else prediction
    edits = edit_distance(scored_prediction, truth)
    cer = edits / len(truth)
    classification = ("ERROR" if error else "EMPTY" if not prediction else
                      "EXACT" if prediction == truth else "MINOR" if cer <= .2 else "SEVERE")
    times = result.get("times_ms", [])
    if any(not isinstance(t, (int, float)) or not math.isfinite(t) or t < 0 for t in times):
        raise ValueError("Invalid engine timing")
    return dict(result, engine=engine, filename=sample["filename"], language=sample["language"],
                category=sample["category"], ground_truth=sample["ground_truth"],
                normalized_ground_truth=truth, normalized_text=prediction,
                image_sha256=sample.get("image_sha256", ""), edits=edits, truth_chars=len(truth),
                cer=cer, character_accuracy=max(0.0, 1.0 - cer), exact=classification == "EXACT",
                classification=classification, median_ms=statistics.median(times) if times else None,
                mean_ms=statistics.mean(times) if times else None,
                p95_ms=percentile95(times) if len(times) >= 20 else None,
                output_stable=len(set(result.get("run_texts", [raw]))) <= 1)


def summarize(rows):
    count = len(rows)
    counts = {key: sum(r["classification"] == key for r in rows)
              for key in ("EXACT", "MINOR", "SEVERE", "EMPTY", "ERROR")}
    chars = sum(r["truth_chars"] for r in rows)
    cer = sum(r["edits"] for r in rows) / chars if chars else None
    timings = [r["median_ms"] for r in rows if not r.get("error") and r["median_ms"] is not None]
    runs = [t for r in rows if not r.get("error") for t in r.get("times_ms", [])]
    return dict(count=count, counts=counts, exact_match_rate=counts["EXACT"] / count if count else None,
                micro_cer=cer, normalized_character_accuracy=max(0., 1. - cer) if cer is not None else None,
                severe_failures=counts["SEVERE"] + counts["ERROR"],
                warm_sample_median_ms=statistics.median(timings) if timings else None,
                warm_sample_mean_ms=statistics.mean(timings) if timings else None,
                warm_sample_p95_ms=percentile95(timings) if len(timings) >= 20 else None,
                warm_run_mean_ms=statistics.mean(runs) if runs else None,
                warm_run_median_ms=statistics.median(runs) if runs else None,
                warm_run_min_ms=min(runs) if runs else None,
                warm_run_max_ms=max(runs) if runs else None,
                warm_runs_above_300ms=sum(t > 300 for t in runs))


def cell(value):
    return html.escape(str(value)).replace("|", "&#124;").replace("\n", "<br>")


def write_reports(output_dir, report):
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "latest.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    fields = ["filename", "language", "category", "engine", "ground_truth", "normalized_ground_truth", "text", "normalized_text",
              "image_sha256", "classification", "cer", "character_accuracy", "exact", "median_ms", "mean_ms",
              "p95_ms", "output_stable", "error", "input_width", "input_height", "processed_width", "processed_height",
              "preprocess", "psm", "warmup_ms", "preprocessing_ms", "recognition_ms", "tesseract_language", "tessdata",
              "box_count", "scores", "boxes", "raw_box_texts", "times_ms", "run_texts"]
    with (output_dir / "latest.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for row in report["rows"]:
            writer.writerow({key: json.dumps(value, ensure_ascii=False) if isinstance(value, (list, dict)) else value
                             for key, value in row.items()})
    lines = ["# OCR Benchmark", "", f"Status: **{cell(report['status'])}**", "",
             "Synthetic samples cannot establish scene-text quality. All manifest rows, including engine failures, are counted.",
             "", "## Aggregate", "", "Engine | N | Exact | CER | Severe+error | Empty | Errors | Warm median ms",
             "--- | --- | --- | --- | --- | --- | --- | ---"]
    for name, summary in report["summaries"].items():
        lines.append(" | ".join(cell(x) for x in [name, summary["count"], summary["exact_match_rate"],
            summary["micro_cer"], summary["severe_failures"], summary["counts"]["EMPTY"],
            summary["counts"]["ERROR"], summary["warm_sample_median_ms"]]))
    names = list(report["engines"])
    lines += ["", "## Same-input Results", "", " | ".join(["Filename", "Category", "Ground Truth"] +
              [f"{cell(name)} text / CER / median ms" for name in names]),
              " | ".join(["---"] * (3 + len(names)))]
    for sample in report["samples"]:
        values = [sample["filename"], sample["category"], sample["ground_truth"]]
        for name in names:
            row = next(r for r in report["rows"] if r["filename"] == sample["filename"] and r["engine"] == name)
            values.append(f"{row['text']} / {row['cer']:.3f} / {row['median_ms']} ({row['classification']})")
        lines.append(" | ".join(cell(value) for value in values))
    lines += ["", "## Per Language And Category", ""]
    for name in names:
        selected = [r for r in report["rows"] if r["engine"] == name]
        for key in ("language", "category"):
            for group in sorted({r[key] for r in selected}):
                summary = summarize([r for r in selected if r[key] == group])
                lines.append(f"- {cell(name)} / {key}={cell(group)}: {cell(json.dumps(summary))}")
    lines += ["", "## Failures (All, Not Only Selected Examples)", ""]
    for row in report["rows"]:
        if row["classification"] in {"SEVERE", "EMPTY", "ERROR"}:
            lines.append(f"- {cell(row['engine'])} / {cell(row['filename'])}: GT={cell(row['ground_truth'])}; "
                         f"OCR={cell(row['text'])}; CER={row['cer']:.3f}; ms={row['median_ms']}; "
                         f"error={cell(row.get('error', ''))}")
    lines += ["", "## Runtime Metadata", "", "```json", json.dumps(report["engines"], ensure_ascii=False, indent=2), "```"]
    (output_dir / "latest.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
