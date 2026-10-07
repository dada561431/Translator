import json
from pathlib import Path
import tempfile
import unittest
from prepare_audio_runtime import validate_inputs


class AudioDeploymentSafety(unittest.TestCase):
    def test_existing_destination_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root / "output"
            output.mkdir()
            sentinel = output / "keep.txt"
            sentinel.write_text("keep", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "new directory"):
                validate_inputs(root / "base", root / "build", root / "model/base.bin", output)
            self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep")

    def test_overlap_rejected_before_model_reads(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaisesRegex(ValueError, "overlap"):
                validate_inputs(root / "base", root / "build", root / "model/base.bin", root / "base/child")

    def test_no_whisper_build_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build = root / "build"
            build.mkdir()
            (build / "audio-build.json").write_text(json.dumps(dict(backend="unavailable")), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Whisper-enabled"):
                validate_inputs(root / "base", build, root / "model/base.bin", root / "output")
