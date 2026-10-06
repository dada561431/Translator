"""Hash-pinned Microsoft runtime inputs; identity is not redistribution clearance."""
import copy
import json
from pathlib import Path
from portable_runtime import digest


def load_vc_inputs(root, catalog=None):
    from validate_windows_package import relative_file
    root = Path(root).resolve()
    if catalog is None:
        catalog = json.loads(Path(__file__).with_name("vc-runtime.json").read_text(encoding="utf-8"))
    record = json.loads((root / "redistribution.json").read_text(encoding="utf-8"))
    for field in ("version", "source_url", "installer_sha256", "license_source_url", "license_sha256", "license_file"):
        if record.get(field) != catalog[field]:
            raise ValueError("Unreviewed VC provenance: " + field)
    expected = {i["path"]: i for i in catalog["files"]}
    actual = {i["path"]: i for i in record["files"]}
    if len(actual) != len(record["files"]) or actual.keys() != expected.keys():
        raise ValueError("Unexpected or duplicate VC runtime inputs")
    license_file = relative_file(root, record["license_file"])
    if digest(license_file) != catalog["license_sha256"]:
        raise ValueError("VC license hash mismatch")
    for name, item in actual.items():
        source = relative_file(root, name)
        pinned = expected[name]
        if (item["version"] != catalog["version"] or item["sha256"] != pinned["sha256"] or item.get("bytes") != pinned["bytes"]
                or digest(source) != pinned["sha256"] or source.stat().st_size != pinned["bytes"]):
            raise ValueError("VC runtime input/version/hash mismatch")
    # The catalog is technical provenance only; an input flag cannot grant owner clearance.
    record = copy.deepcopy(catalog)
    record["review_status"] = "OWNER REVIEW REQUIRED"
    record["owner_redistribution_confirmed"] = False
    return record
