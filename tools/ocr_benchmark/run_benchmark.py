"""Local same-input A/B benchmark. Never imported by the Translator application."""
import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
from pathlib import Path

from metrics import evaluate, load_manifest, summarize, write_reports

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "benchmarks" / "ocr_phase61a"


def execute(command, request, work, name, env, timeout):
    request_path, response_path = work / f"{name}.request.json", work / f"{name}.response.json"
    request_path.write_text(json.dumps(request, ensure_ascii=False), encoding="utf-8")
    # Never reuse stale worker output after a failed/terminated subprocess.
    if response_path.exists():
        response_path.unlink()
    try:
        with (work / f"{name}.log").open("w", encoding="utf-8") as log:
            completed = subprocess.run([*command, str(request_path), str(response_path)],
                env=env, stdout=log, stderr=subprocess.STDOUT, timeout=timeout, check=False)
        if completed.returncode:
            raise RuntimeError(f"Worker exit code {completed.returncode}; see local {name}.log")
        response = json.loads(response_path.read_text(encoding="utf-8"))
        expected = {s["filename"] for s in request["samples"]}
        actual = [row["filename"] for row in response["rows"]]
        if set(actual) != expected or len(actual) != len(expected):
            raise ValueError("Worker did not return exactly one result per manifest sample")
        return response
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, subprocess.TimeoutExpired) as error:
        return dict(startup_error=str(error), rows=[dict(filename=s["filename"], text="",
            error=str(error), times_ms=[]) for s in request["samples"]])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DATA / "private" / "manifest.csv")
    parser.add_argument("--images", type=Path, default=DATA / "private")
    parser.add_argument("--output", type=Path, default=DATA / "results")
    parser.add_argument("--models", type=Path, default=DATA / "models")
    parser.add_argument("--tesseract-exe", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--cpu-threads", type=int, default=4)
    parser.add_argument("--no-mkldnn", action="store_true")
    parser.add_argument("--synthetic", action="store_true")
    parser.add_argument("--ground-truth-confirmed", action="store_true")
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()
    if not 3 <= args.repeats <= 100 or args.cpu_threads < 1:
        parser.error("Use 3..100 repeats and at least one CPU thread")
    try:
        samples = load_manifest(args.manifest, args.images)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    for sample in samples:
        with Path(sample["path"]).open("rb") as stream:
            sample["image_sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
    args.output.mkdir(parents=True, exist_ok=True)
    work = args.output / "raw"
    work.mkdir(exist_ok=True)
    env = os.environ.copy()
    cache = DATA / ".cache"
    for variable, folder in (("PADDLE_PDX_CACHE_HOME", "paddlex"), ("HF_HOME", "huggingface"),
                             ("MODELSCOPE_CACHE", "modelscope"), ("PADDLE_HOME", "paddle")):
        env[variable] = str(cache / folder)
    env["PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK"] = "True"
    env["HF_HUB_OFFLINE"] = "1"
    env["HF_HUB_DISABLE_TELEMETRY"] = "1"
    env["PYTHONIOENCODING"] = "utf-8"
    request = dict(samples=[{key: s[key] for key in ("filename", "path", "language")} for s in samples],
                   repeats=args.repeats, models=str(args.models.resolve()),
                   cpu_threads=args.cpu_threads, mkldnn=not args.no_mkldnn)
    engines = {}
    commands = {"production_tesseract": [str(args.tesseract_exe.resolve())],
                "PP-OCRv6_small": [sys.executable, str(Path(__file__).with_name("paddle_worker.py"))],
                "PP-OCRv6_medium": [sys.executable, str(Path(__file__).with_name("paddle_worker.py"))]}
    rows = []
    for name, command in commands.items():
        print(f"Running {name}: {len(samples)} samples, {args.repeats} warm repeats", flush=True)
        response = execute(command, dict(request, tier=name.rsplit("_", 1)[-1]), work, name, env, args.timeout)
        engines[name] = {key: value for key, value in response.items() if key != "rows"}
        indexed = {r["filename"]: r for r in response["rows"]}
        rows.extend(evaluate(sample, indexed[sample["filename"]], name) for sample in samples)
    failures = any(row.get("error") for row in rows)
    status = ("SYNTHETIC_SMOKE_ONLY" if args.synthetic else
              "INSUFFICIENT_REAL_SAMPLES" if len(samples) < 10 else
              "GROUND_TRUTH_CONFIRMATION_PENDING" if not args.ground_truth_confirmed else
              "ENGINE_FAILURES" if failures else "REAL_AB_COMPLETE_LIMITED_COVERAGE")
    for sample in samples:
        with Path(sample["path"]).open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != sample["image_sha256"]:
                raise RuntimeError("Input changed during benchmark; discard this run")
    model_index = args.models / "index.json"
    report = dict(status=status, samples=samples, rows=rows, engines=engines,
        summaries={name: summarize([r for r in rows if r["engine"] == name]) for name in engines},
        environment=dict(python=sys.version, os=platform.platform(), machine=platform.machine(),
                         logical_cpus=os.cpu_count(), repeats=args.repeats,
                         cpu_threads=args.cpu_threads, mkldnn=not args.no_mkldnn),
        models=json.loads(model_index.read_text(encoding="utf-8")) if model_index.exists() else None)
    write_reports(args.output, report)
    print(json.dumps(dict(status=status, summaries=report["summaries"]), ensure_ascii=False, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
