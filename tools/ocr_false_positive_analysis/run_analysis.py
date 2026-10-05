"""Run the production C++ parser/engine and analyze private, human-labelled crops."""
import argparse
import csv
import json
import hashlib
import subprocess
import tempfile
from pathlib import Path
from analysis import analyze, load_manifest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--images", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-incomplete", action="store_true")
    parser.add_argument("--labels-reviewed", action="store_true")
    args = parser.parse_args()
    samples = load_manifest(args.manifest, args.images)
    counts = {kind: sum(row["type"] == kind for row in samples) for kind in ("positive", "negative")}
    if not args.allow_incomplete and (min(counts.values()) < 15 or not args.labels_reviewed):
        parser.error("Need >=15 distinct positives and negatives with human-confirmed labels; use --allow-incomplete for preliminary analysis")
    # Enforce local ignored output locations, not arbitrary tracked report paths.
    output = args.output.resolve()
    if subprocess.run(["git", "check-ignore", "-q", str(output / "analysis.json")], cwd=Path(__file__).resolve().parents[2]).returncode:
        parser.error("Output must be Git-ignored; use benchmarks/ocr_false_positive/results/")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="Translator-OcrAnalysis-") as temporary:
        raw = Path(temporary) / "native.json"
        completed = subprocess.run([str(args.probe.resolve()), "--batch", str(raw),
            *[row["path"] for row in samples]], timeout=max(120, len(samples) * 5),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        # No DeepL, screenshot uploads, new inference implementation or model download.
        (output / "native.stderr.log").write_bytes(completed.stderr)
        (output / "native.stdout.log").write_bytes(completed.stdout)
        if not raw.is_file():
            raise RuntimeError("Production probe failed before producing a report")
        data = json.loads(raw.read_text(encoding="utf-8"))
        native = data["samples"]
        if len(native) != 2 * len(samples):
            raise RuntimeError("Incomplete native report: all samples must remain in the denominator")
        rows = []
        for index, sample in enumerate(samples):
            if hashlib.sha256(Path(sample["path"]).read_bytes()).hexdigest() != sample["image_sha256"]:
                raise RuntimeError("Input changed during inference; repeat with stable crops")
            result = native[index * 2 + 1]
            if Path(result["filename"]).resolve() != Path(sample["path"]):
                raise RuntimeError("Native sample identity mismatch")
            if not result.get("error") and result["engine"] != "paddle-small":
                raise RuntimeError("Unexpected production engine")
            rows.append(dict(sample, **{k: v for k, v in result.items() if k != "filename"}))
        report = analyze(rows)
        report.update(counts=counts, labels_reviewed=args.labels_reviewed,
            preliminary=min(counts.values()) < 15 or not args.labels_reviewed,
            helper_pids=sorted({r["helper_pid"] for r in native if r.get("helper_pid")}),
            native_returncode=completed.returncode,
            detection_score_status=sorted({r.get("detection_score_status", "missing") for r in native}))
        (output / "native.json").write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
        (output / "analysis.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        with (output / "confidence-sweep.csv").open("w", encoding="utf-8-sig", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(report["raw"]))
            writer.writeheader(); writer.writerows(report["confidence_sweep"])
        with (output / "geometry-sweep.csv").open("w", encoding="utf-8-sig", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(report["raw"]))
            writer.writeheader(); writer.writerows(report["geometry_sweep"])
        lines = ["# Local OCR Analysis", f"Preliminary: {report['preliminary']}",
            f"Samples: {counts}", "", "| Threshold | Negative FP | Positive Exact | Accepted | Rejected | CER |",
            "| --- | --- | --- | --- | --- | --- |"]
        for row in report["confidence_sweep"]:
            lines.append(f"| {row['threshold']} | {row['negative_fp']} | {row['positive_exact']} | "
                f"{row['positive_accepted']} | {row['positive_rejected']} | {row['positive_cer']} |")
        lines += ["", "Distributions:", "```json", json.dumps(report["distributions"], indent=2), "```",
                  "", report["temporal_experiment"], report["decision"]]
        (output / "analysis.md").write_text("\n".join(lines), encoding="utf-8")
        print(json.dumps(dict(counts=counts, preliminary=report["preliminary"], raw=report["raw"])))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
