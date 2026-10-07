"""Extend the existing portable candidate with explicit, local audio inputs.

No downloads, cleanup, source-model mutation or public release artifact generation.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

from portable_runtime import digest
from validate_windows_package import validate, dependency_audit

PIN = "48f628a84833905ee4a0658ee6d4a5c915ce1997"
MODEL_HASH = "60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe"
MODEL_SIZE = 147951465


def validate_inputs(base, build, model, output):
    base, build, model, output = map(lambda p: Path(p).resolve(), (base, build, model, output))
    if output.exists():
        raise ValueError("Output must be a new directory; existing contents are never overwritten")
    if any(output == p or output.is_relative_to(p) or p.is_relative_to(output)
           for p in (base, build, model.parent)):
        raise ValueError("Output must not overlap any input")
    metadata = json.loads((build / "audio-build.json").read_text(encoding="utf-8"))
    if metadata != dict(backend="whisper.cpp", linkage="static", commit=PIN):
        raise ValueError("A pinned Whisper-enabled production build is required")
    if not (build / "Translator.exe").is_file():
        raise ValueError("Translator.exe missing")
    if model.stat().st_size != MODEL_SIZE or digest(model) != MODEL_HASH:
        raise ValueError("Expected the already verified multilingual ggml-base model")
    return base, build, model, output


def prepare(base, build, model, output, qt_root, mingw_root):
    base, build, model, output = validate_inputs(base, build, model, output)
    manifest = validate(base)
    qt, mingw = Path(qt_root).resolve(), Path(mingw_root).resolve()
    deploy = qt / "bin/windeployqt.exe"
    if not deploy.is_file():
        raise ValueError("windeployqt missing")
    shutil.copytree(base, output)
    shutil.copy2(build / "Translator.exe", output / "Translator.exe")
    env = os.environ.copy()
    env["PATH"] = str(mingw / "bin") + os.pathsep + str(qt / "bin") + os.pathsep + env.get("PATH", "")
    subprocess.run([str(deploy), "--release", "--no-translations", "--no-system-d3d-compiler",
                    "--no-opengl-sw", "--no-quick-import", "--dir", str(output),
                    str(output / "Translator.exe")], env=env, check=True)
    for name in ("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll"):
        shutil.copy2(mingw / "bin" / name, output / name)
    (output / "models").mkdir(exist_ok=True)
    destination = output / "models/ggml-base.bin"
    shutil.copy2(model, destination)
    if digest(destination) != MODEL_HASH:
        raise ValueError("Model changed during copy")
    if not (output / "Qt6Multimedia.dll").is_file() or not (output / "multimedia").is_dir():
        raise ValueError("Qt Multimedia deployment incomplete")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=Path(__file__).resolve().parent.parent, text=True).strip()
    manifest.update(commit=commit, source_dirty=bool(subprocess.check_output(
        ["git", "diff", "--name-only"], cwd=Path(__file__).resolve().parent.parent, text=True).strip()),
        audio=dict(backend="whisper.cpp", whisper_commit=PIN, linkage="static", model="models/ggml-base.bin",
                   model_sha256=MODEL_HASH, model_bytes=MODEL_SIZE, public_redistribution="OWNER REVIEW REQUIRED"),
        clean_machine_acceptance="pending for audio candidate", physically_offline_acceptance="pending for audio candidate")
    manifest["files"] = [dict(path=p.relative_to(output).as_posix(), bytes=p.stat().st_size, sha256=digest(p))
                         for p in sorted(output.rglob("*")) if p.is_file() and p.name != "runtime-manifest.json"]
    (output / "runtime-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    validate(output)
    audit = dependency_audit(output)
    # Keep diagnostics outside the strict package manifest.
    report = output.with_name(output.name + "-dependency-audit.json")
    report.write_text(json.dumps(audit, indent=2), encoding="utf-8")
    if audit["missing_count"]:
        raise ValueError(f"Missing dependency candidates: {audit['missing_count']}; see {report}")
    print(json.dumps(dict(output=str(output), model_sha256=MODEL_HASH, model_bytes=MODEL_SIZE,
                          whisper_commit=PIN, missing_dependencies=0, public_release=False)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("base-package", "build-dir", "model", "output-dir", "qt-root", "mingw-root"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    prepare(args.base_package, args.build_dir, args.model, args.output_dir, args.qt_root, args.mingw_root)


if __name__ == "__main__":
    main()
