"""Explicit download of official inference weights, never user images."""
import argparse
import hashlib
import json
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / "benchmarks/ocr_phase61a"
SOURCE = "https://paddle-model-ecology.bj.bcebos.com/paddlex/official_inference_model/paddle3.0.0/"


def prepare(root):
    root.mkdir(parents=True, exist_ok=True)
    entries = []
    for tier in ("small", "medium"):
        for kind in ("det", "rec"):
            name = f"PP-OCRv6_{tier}_{kind}"
            archive = root / f"{name}_infer.tar"
            url = f"{SOURCE}{archive.name}"
            if not archive.is_file():
                print(f"Downloading {name} from official model storage", flush=True)
                with urllib.request.urlopen(url, timeout=120) as response, archive.with_suffix(".part").open("wb") as out:
                    while block := response.read(1024 * 1024):
                        out.write(block)
                archive.with_suffix(".part").replace(archive)
            folder = root / name
            with tarfile.open(archive) as bundle:
                bundle.extractall(folder, filter="data")
            configs = list(folder.rglob("inference.yml"))
            if len(configs) != 1:
                raise ValueError(f"Unexpected inference package layout for {name}")
            files = []
            for path in sorted(configs[0].parent.rglob("*")):
                if path.is_file():
                    with path.open("rb") as stream:
                        digest = hashlib.file_digest(stream, "sha256").hexdigest()
                    files.append(dict(path=str(path.relative_to(root)), size_bytes=path.stat().st_size,
                                      sha256=digest))
            with archive.open("rb") as stream:
                archive_sha = hashlib.file_digest(stream, "sha256").hexdigest()
            entries.append(dict(name=name, source=url, model_dir=str(configs[0].parent.resolve()),
                                archive_bytes=archive.stat().st_size, archive_sha256=archive_sha,
                                extracted_bytes=sum(f["size_bytes"] for f in files), files=files))
    (root / "index.json").write_text(json.dumps(entries, indent=2), encoding="utf-8")
    print(json.dumps([{k: v for k, v in entry.items() if k != "files"} for entry in entries], indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "models")
    prepare(parser.parse_args().output.resolve())
