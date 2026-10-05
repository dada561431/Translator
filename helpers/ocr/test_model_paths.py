import hashlib
from pathlib import Path
import tempfile
import unittest
from paddle_helper import native_model_directory


class ModelPathTests(unittest.TestCase):
    def test_ascii_does_not_stage(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, cache = root / "model", root / "cache"
            source.mkdir()
            self.assertEqual(native_model_directory(source, cache), str(source.resolve()))
            self.assertFalse(cache.exists())

    @unittest.skipUnless(__import__("os").name == "nt", "Windows native path adapter")
    def test_unicode_exact_copy_reuse_and_repair(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "model \u4e2d\u6587"
            source.mkdir()
            files = ("inference.yml", "inference.json", "inference.pdiparams")
            for file in files:
                (source / file).write_bytes(file.encode("ascii"))
            hashes = {f: hashlib.sha256((source / f).read_bytes()).hexdigest() for f in files}
            cached = Path(native_model_directory(source, root / "cache"))
            self.assertTrue(str(cached).isascii())
            for file in files:
                self.assertEqual((source / file).read_bytes(), (cached / file).read_bytes())
            self.assertEqual(native_model_directory(source, root / "cache"), str(cached))
            (cached / files[0]).write_bytes(b"corrupt")
            self.assertEqual(native_model_directory(source, root / "cache"), str(cached))
            for file in files:
                self.assertEqual(hashlib.sha256((cached / file).read_bytes()).hexdigest(), hashes[file])
                self.assertEqual(hashlib.sha256((source / file).read_bytes()).hexdigest(), hashes[file])
