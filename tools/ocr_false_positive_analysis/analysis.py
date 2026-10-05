"""Offline evaluation of raw production metadata; no inference or default policy."""
import csv
import hashlib
import math
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "helpers" / "ocr"))
sys.path.insert(0, str(ROOT / "tools" / "ocr_benchmark"))
from paddle_helper import reading_order
from metrics import edit_distance, normalize

THRESHOLDS = (0.0, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9)


def load_manifest(path, image_root):
    root = Path(image_root).resolve()
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if not {"filename", "type", "ground_truth"} <= set(reader.fieldnames or []):
            raise ValueError("Required manifest fields: filename,type,ground_truth")
        rows, paths, hashes = [], set(), set()
        for index, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f"Malformed manifest row {index}")
            image = (root / row["filename"]).resolve()
            if not row["filename"] or Path(row["filename"]).is_absolute() or not image.is_relative_to(root):
                raise ValueError(f"Unsafe image path at row {index}")
            if not image.is_file() or image in paths:
                raise ValueError(f"Missing or repeated image at row {index}")
            digest = hashlib.sha256(image.read_bytes()).hexdigest()
            if digest in hashes:
                raise ValueError(f"Duplicate image content at row {index}")
            paths.add(image); hashes.add(digest)
            kind, truth = row["type"], normalize(row["ground_truth"])
            if kind not in {"positive", "negative"}:
                raise ValueError(f"Invalid sample type at row {index}")
            if (kind == "positive" and not truth) or (kind == "negative" and truth):
                raise ValueError(f"Ground truth conflicts with sample type at row {index}")
            rows.append(dict(row, path=str(image), image_sha256=digest))
    if not rows:
        raise ValueError("No samples; positive and negative video crops are required")
    return rows


def distribution(values, bucket_scores=False):
    values = [float(value) for value in values]
    if not values:
        return dict(count=0, min=None, median=None, mean=None, max=None)
    if any(not math.isfinite(value) for value in values):
        raise ValueError("Non-finite metadata")
    result = dict(count=len(values), min=min(values), median=statistics.median(values),
                  mean=statistics.mean(values), max=max(values))
    if bucket_scores:
        edges = (0.0, 0.2, 0.4, 0.6, 0.8, 1.0)
        result["buckets"] = [sum((edges[i] <= value < edges[i+1])
            or (i == 4 and value == 1) for value in values) for i in range(5)]
    return result


def reassemble(result, threshold=0.0, minimum_relative_area=0.0):
    if result.get("error"):
        return "", 0, 0
    if not result.get("box_texts_available"):
        raise ValueError("Production helper lacks per-box text metadata; cannot fairly sweep mixed boxes")
    boxes = result["results"]
    kept = [box for box in boxes if box["score"] >= threshold
            and box["relative_area"] >= minimum_relative_area]
    ordered = reading_order([box["text"] for box in kept], [box["box"] for box in kept],
                            [box["score"] for box in kept])
    return normalize(ordered["text"]), len(kept), len(boxes) - len(kept)


def summarize(rows, threshold=0.0, minimum_relative_area=0.0):
    summary = dict(threshold=threshold, minimum_relative_area=minimum_relative_area,
        positive_total=0, positive_exact=0, positive_minor=0, positive_severe=0,
        positive_accepted=0, positive_rejected=0, positive_errors=0, negative_total=0,
        negative_valid=0, negative_fp=0, negative_errors=0, accepted_boxes=0, rejected_boxes=0)
    edits, truth_chars = 0, 0
    for row in rows:
        prediction, accepted, rejected = reassemble(row, threshold, minimum_relative_area)
        summary["accepted_boxes"] += accepted; summary["rejected_boxes"] += rejected
        if row["type"] == "negative":
            summary["negative_total"] += 1
            summary["negative_errors"] += bool(row.get("error"))
            summary["negative_valid"] += not bool(row.get("error"))
            summary["negative_fp"] += bool(prediction)
        else:
            summary["positive_total"] += 1
            summary["positive_errors"] += bool(row.get("error"))
            summary["positive_accepted"] += bool(prediction)
            summary["positive_rejected"] += not bool(prediction) and not bool(row.get("error"))
            truth = normalize(row["ground_truth"])
            distance = edit_distance(prediction, truth)
            truth_chars += len(truth); edits += distance
            key = "positive_exact" if prediction == truth else (
                "positive_minor" if prediction and distance / len(truth) <= .2 else "positive_severe")
            summary[key] += 1
    summary["positive_cer"] = edits / truth_chars if truth_chars else None
    valid = summary["negative_valid"]
    summary["negative_fp_frame_rate"] = summary["negative_fp"] / valid if valid else None
    return summary


def analyze(rows):
    distributions = {}
    for kind in ("positive", "negative"):
        boxes = [box for row in rows if row["type"] == kind and not row.get("error") for box in row["results"]]
        distributions[kind] = {field: distribution([box[field] for box in boxes], field == "score")
            for field in ("score", "relative_width", "relative_height", "relative_area",
                          "relative_center_x", "relative_center_y")}
        distributions[kind]["characters"] = distribution([len(box["text"]) for box in boxes])
    return dict(distributions=distributions, raw=summarize(rows),
        confidence_sweep=[summarize(rows, threshold) for threshold in THRESHOLDS],
        geometry_sweep=[summarize(rows, threshold, area) for threshold in THRESHOLDS
            for area in (0.001, 0.003, 0.01, 0.03)],
        temporal_experiment="not evaluated: requires ordered video transitions, not shuffled still crops",
        decision="pending: no automatic production threshold selection", samples=rows)
