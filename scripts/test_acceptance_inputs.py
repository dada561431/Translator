import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from prepare_vc_runtime import cabinet_ranges
from vc_runtime_inputs import load_vc_inputs
from validate_windows_package import validate_model_hashes


class AcceptanceInputTests(unittest.TestCase):
    def test_burn_ranges_and_invalid_headers(self):
        header = struct.pack("<II16s8I", 0x00F14300, 2, b"\0" * 16, 100, 0, 140, 10, 1, 2, 40, 50)
        self.assertEqual(cabinet_ranges(header, 200), [(100, 40), (150, 50)])
        for invalid, size in ((header[:20], 200), (b"bad!" + header[4:], 200), (header, 199)):
            with self.assertRaises(ValueError):
                cabinet_ranges(invalid, size)
        overlap = bytearray(header)
        struct.pack_into("<I", overlap, 32, 120)
        with self.assertRaises(ValueError):
            cabinet_ranges(overlap, 200)

    def fixture(self, root):
        payload = b"fixture, not a DLL"
        sha = hashlib.sha256(payload).hexdigest()
        (root / "msvcp140.dll").write_bytes(payload)
        (root / "LICENSE.txt").write_bytes(payload)
        catalog = dict(version="fixture", source_url="https://aka.ms/vc14/vc_redist.x64.exe",
                       installer_sha256="fixture", license_source_url="https://visualstudio.microsoft.com/fixture",
                       license_sha256=sha, license_file="LICENSE.txt",
                       files=[dict(path="msvcp140.dll", version="fixture", sha256=sha, bytes=len(payload))])
        return catalog

    def test_vc_identity_cannot_grant_owner_permission(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            catalog = self.fixture(root)
            record = dict(catalog, owner_redistribution_confirmed=True, review_status="cleared", arbitrary_extra="not shipped")
            (root / "redistribution.json").write_text(json.dumps(record))
            result = load_vc_inputs(root, catalog)
            self.assertFalse(result["owner_redistribution_confirmed"])
            self.assertEqual(result["review_status"], "OWNER REVIEW REQUIRED")
            self.assertNotIn("arbitrary_extra", result)

    def test_vc_untrusted_provenance_or_mixed_version_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            catalog = self.fixture(root)
            for key, value in (("source_url", "https://third-party.invalid/dll"), ("version", "latest"),
                               ("license_file", "../outside"), ("license_sha256", "bad")):
                record = dict(catalog, **{key: value})
                (root / "redistribution.json").write_text(json.dumps(record))
                with self.assertRaises(ValueError):
                    load_vc_inputs(root, catalog)
            record = copy.deepcopy(catalog)
            record["files"][0]["version"] = "different"
            (root / "redistribution.json").write_text(json.dumps(record))
            with self.assertRaises(ValueError):
                load_vc_inputs(root, catalog)

    def test_vc_modified_missing_or_duplicate_bytes_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            catalog = self.fixture(root)
            (root / "redistribution.json").write_text(json.dumps(catalog))
            (root / "msvcp140.dll").write_bytes(b"modified")
            with self.assertRaises(ValueError):
                load_vc_inputs(root, catalog)
            (root / "msvcp140.dll").unlink()
            with self.assertRaises(ValueError):
                load_vc_inputs(root, catalog)
            record = copy.deepcopy(catalog)
            record["files"].append(record["files"][0])
            (root / "redistribution.json").write_text(json.dumps(record))
            with self.assertRaises(ValueError):
                load_vc_inputs(root, catalog)

    def test_model_identity_is_independent_of_package_manifest(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            model = root / "ocr/models/model/inference.pdiparams"
            model.parent.mkdir(parents=True)
            model.write_bytes(b"verified")
            catalog = dict(files=[dict(path="model/inference.pdiparams", bytes=8,
                                      sha256=hashlib.sha256(b"verified").hexdigest())])
            validate_model_hashes(root, catalog)
            model.write_bytes(b"modified")
            with self.assertRaises(ValueError):
                validate_model_hashes(root, catalog)


if __name__ == "__main__":
    unittest.main()
