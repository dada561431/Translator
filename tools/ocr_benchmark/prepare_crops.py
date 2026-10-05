"""Explicit local crop utility; coordinates and visual labels come from a private JSON file."""
import csv
import hashlib
import json
import sys
from pathlib import Path
from PIL import Image


def prepare(acquisition_path):
    acquisition_path = Path(acquisition_path)
    spec = json.loads(acquisition_path.read_text(encoding="utf-8"))
    destination = acquisition_path.parent
    seen, manifest, provenance = {}, [], []
    for item in spec["samples"]:
        source = Path(item["source"])
        target = (destination / item["filename"]).resolve()
        if not item["filename"] or Path(item["filename"]).is_absolute() or not target.is_relative_to(destination.resolve()):
            raise ValueError("Crop filename must stay within the local acquisition directory")
        with source.open("rb") as stream:
            source_hash = hashlib.file_digest(stream, "sha256").hexdigest()
        if source_hash in seen:
            provenance.append(dict(source=str(source), duplicate_of=seen[source_hash]))
            continue
        seen[source_hash] = item["filename"]
        with Image.open(source) as image:
            left, top, right, bottom = item["crop"]
            if not (0 <= left < right <= image.width and 0 <= top < bottom <= image.height):
                raise ValueError("Crop outside input bounds")
            cropped = image.crop((left, top, right, bottom))
            cropped.save(target)
            provenance.append(dict(source=str(source), source_sha256=source_hash,
                source_size=list(image.size), filename=item["filename"], crop=item["crop"],
                crop_size=list(cropped.size)))
        manifest.append({key: item[key] for key in ("filename", "language", "ground_truth", "category")})
    with (destination / "manifest.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=["filename", "language", "ground_truth", "category"])
        writer.writeheader()
        writer.writerows(manifest)
    (destination / "provenance.json").write_text(json.dumps(dict(source=spec.get("source"),
        label_method="Independent visual transcription; user confirmation recorded separately",
        items=provenance), ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Prepared {len(manifest)} unique crops; duplicates recorded, not counted twice")


if __name__ == "__main__":
    prepare(sys.argv[1])
