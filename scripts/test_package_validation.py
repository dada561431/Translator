import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from portable_runtime import VERSIONS, is_build_only
from validate_windows_package import REQUIRED, validate


class PackageValidationTests(unittest.TestCase):
    def test_build_tools_excluded_without_removing_inference_dependencies(self):
        for name in ("pip", "pip-24.0.dist-info", "PyInstaller", "_pyinstaller_hooks_contrib",
                     "pyinstaller_hooks_contrib-2026.8.dist-info", "pefile.py", "peutils.py",
                     "pywin32_ctypes-0.2.3.dist-info", "win32ctypes"):
            self.assertTrue(is_build_only(name), name)
        for name in ("paddle", "paddlex", "paddleocr", "setuptools", "numpy", "cv2"):
            self.assertFalse(is_build_only(name), name)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="package tests ")
        self.root = Path(self.temp.name) / "portable \u4e2d\u6587"
        self.root.mkdir()
        self.names = list(REQUIRED)
        for kind in ("det", "rec"):
            name = f"PP-OCRv6_small_{kind}"
            self.names.extend(f"ocr/models/{name}/{name}_infer/{file}" for file in
                              ("inference.yml", "inference.json", "inference.pdiparams"))
        self.manifest = dict(schema=1, mkldnn=False, versions=dict(VERSIONS, qt="6.11.2", python="3.12.14"), files=[])
        for name in self.names:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture")
            self.manifest["files"].append(dict(path=name, bytes=7, sha256=hashlib.sha256(b"fixture").hexdigest()))
        self.write_manifest()

    def tearDown(self):
        self.temp.cleanup()

    def write_manifest(self):
        (self.root / "runtime-manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def test_complete_and_unicode_path(self):
        self.assertEqual(validate(self.root)["schema"], 1)

    def test_missing_runtime_files(self):
        for name in ("Qt6Core.dll", "platforms/qwindows.dll", "ocr/helper/paddle_helper.py", self.names[-1]):
            path = self.root / name
            path.unlink()
            with self.assertRaises(ValueError):
                validate(self.root)
            path.write_bytes(b"fixture")

    def test_manifest_hash_mismatch(self):
        (self.root / self.names[0]).write_bytes(b"changed")
        with self.assertRaises(ValueError):
            validate(self.root)

    def test_absolute_and_traversal_paths(self):
        for name in ("C:/outside", "../escape", "ocr\\wrong"):
            self.manifest["files"][0]["path"] = name
            self.write_manifest()
            with self.assertRaises(ValueError):
                validate(self.root)

    def test_duplicate_and_unmanifested(self):
        self.manifest["files"].append(self.manifest["files"][0])
        self.write_manifest()
        with self.assertRaises(ValueError):
            validate(self.root)
        self.manifest["files"].pop()
        self.write_manifest()
        (self.root / "unexpected.log").write_text("unexpected")
        with self.assertRaises(ValueError):
            validate(self.root)

    def test_version_and_mkldnn(self):
        for field, value in (("mkldnn", True), ("schema", 2)):
            original = self.manifest[field]
            self.manifest[field] = value
            self.write_manifest()
            with self.assertRaises(ValueError):
                validate(self.root)
            self.manifest[field] = original
        self.manifest["versions"]["paddlex"] = "latest"
        self.write_manifest()
        with self.assertRaises(ValueError):
            validate(self.root)

    def test_personal_path_leak(self):
        self.manifest["source"] = "D:/workspace/personal"
        self.write_manifest()
        with self.assertRaises(ValueError):
            validate(self.root)


if __name__ == "__main__":
    unittest.main()
