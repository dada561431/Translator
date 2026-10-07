import json
import tempfile
import unittest
import wave
from pathlib import Path

from benchmark_streaming_asr import active_span, annotate_loopback, annotate_recording, evaluate, normalize, utterance
from analyze_streaming_asr_probe import edit_distance


class BenchmarkTest(unittest.TestCase):
    def test_wer_normalization_and_calculation(self):
        self.assertEqual(normalize(" Hello,   WORLD! ", "en"), ["hello", "world"])
        self.assertEqual(edit_distance(normalize("hello world", "en"), normalize("hello word please", "en")), 2)
        self.assertEqual(edit_distance(["a", "b"], []), 2)
        self.assertEqual(normalize("cross-platform", "en"), ["crossplatform"])

    def test_cer_unicode_no_pinyin_or_semantic_correction(self):
        self.assertEqual(normalize("你 好，世界！😀", "zh"), list("你好世界😀"))
        self.assertEqual(edit_distance(normalize("你好", "zh"), normalize("你号", "zh")), 1)

    def test_waveform_boundary_is_not_file_end(self):
        pcm = bytes(160) + (1000).to_bytes(2, "little", signed=True) * 80 + bytes(320)
        self.assertEqual(active_span(pcm), (5, 10))
        with self.assertRaises(ValueError):
            active_span(bytes(640))

    def row(self):
        return dict(id="en01", language="en", reference="Hello world.",
                    window_start_ms=0, window_end_ms=4000, speech_start_ms=100, speech_end_ms=1500)

    def events(self):
        return [dict(event="model_loaded", model="zipformer"),
                dict(event="partial", text="hello", stable="", wall_ms=500, audio_ms=480,
                     segment=0, added_chars=5, removed_replaced_chars=0),
                dict(event="partial", text="hello word", stable="hello", wall_ms=1000, audio_ms=980,
                     segment=0, added_chars=5, removed_replaced_chars=0),
                dict(event="partial", text="hello world", stable="hello wor", wall_ms=1300, audio_ms=1280,
                     segment=0, added_chars=2, removed_replaced_chars=1),
                dict(event="partial", text="fake flush", during_flush=True, wall_ms=1400, audio_ms=1380),
                dict(event="endpoint", wall_ms=2800, audio_ms=2780),
                dict(event="final", text="hello world", wall_ms=2801, audio_ms=2780)]

    def test_stability_rewrite_and_latencies_use_raw_results(self):
        result = utterance(self.events(), self.row())
        self.assertEqual(result["wer"], 0)
        self.assertEqual(result["partial_count"], 3)
        self.assertEqual(result["first_partial_ms"], 400)
        self.assertEqual(result["first_stable_ms"], 900)
        self.assertEqual(result["speech_end_to_final_ms"], 1301)
        self.assertEqual(result["rewrite_chars"], 1)
        self.assertEqual(result["stable_prefix_growth"][-1]["characters"], 9)
        self.assertAlmostEqual(result["rewrite_rate"], 1 / 12)
        self.assertIsNone(result["semantic_acceptable"])

    def test_missing_output_is_deletion_and_unknown_latency(self):
        result = utterance([], self.row())
        self.assertEqual(result["wer"], 1)
        self.assertIsNone(result["first_partial_ms"])

    def test_silence_endpoint_is_not_utterance_endpoint(self):
        events = self.events()
        for e in events:
            if e["event"] in ("endpoint", "final"):
                e["segment"] = 2
        events.insert(0, dict(event="endpoint", segment=1, wall_ms=80, audio_ms=60))
        events.insert(1, dict(event="final", segment=1, text="", wall_ms=81, audio_ms=60))
        result = utterance(events, self.row())
        self.assertEqual(result["speech_end_to_endpoint_ms"], 1300)

    def test_serialization_and_no_cross_utterance_leak(self):
        events = self.events() + [dict(event="final", text="wrong other utterance", audio_ms=4000)]
        result = json.loads(json.dumps(evaluate(events, [self.row()])))
        self.assertEqual(result["schema_version"], 1)
        self.assertEqual(result["accuracy_summary"]["en"]["mean"], 0)
        self.assertEqual(result["utterances"][0]["final"], "hello world")
        self.assertIsNone(result["accuracy_summary"]["first_partial_p90_ms"])

    def test_loopback_annotation_rejects_continuous_background_activity(self):
        with tempfile.TemporaryDirectory() as directory:
            wav = Path(directory) / "input.wav"
            output = Path(directory) / "manifest.json"
            with wave.open(str(wav), "wb") as audio:
                audio.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
                audio.writeframes((1000).to_bytes(2, "little", signed=True) * 64000)
            manifest = [dict(self.row(), window_end_ms=4000)]
            with self.assertRaisesRegex(ValueError, "silent margins"):
                annotate_loopback(manifest, wav, [{"event": "playback_start", "wall_ms": 0}], output)
            self.assertFalse(output.exists())

    def test_explicit_reference_windows_preserve_repeated_speech(self):
        with tempfile.TemporaryDirectory() as directory:
            wav = Path(directory) / "input.wav"
            output = Path(directory) / "manifest.json"
            with wave.open(str(wav), "wb") as audio:
                audio.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
                audio.writeframes(bytes(32000) + (1000).to_bytes(2, "little", signed=True) * 16000 + bytes(64000))
            window = dict(self.row(), reference="Hello world. Hello world.",
                          ground_truth_status="OWNER CONFIRMED")
            del window["speech_start_ms"]
            del window["speech_end_ms"]
            annotate_recording([window], wav, output, "explicit QA", windows=[window], threshold=.005)
            result = json.loads(output.read_text(encoding="utf-8"))[0]
            self.assertEqual(result["reference"], window["reference"])
            self.assertEqual(result["speech_start_ms"], 1000)
            self.assertEqual(result["speech_end_ms"], 2000)
            self.assertEqual(result["ground_truth_status"], "OWNER CONFIRMED")
            annotated = dict(window, speech_start_ms=1100, speech_end_ms=1900)
            override = Path(directory) / "override.json"
            annotate_recording([annotated], wav, override, "explicit QA", windows=[annotated])
            self.assertEqual(json.loads(override.read_text())[0]["speech_end_ms"], 1900)
            with self.assertRaisesRegex(ValueError, "overlapping"):
                annotate_recording([window, window], wav, Path(directory) / "bad.json",
                                   "explicit QA", windows=[window, window])


if __name__ == "__main__":
    unittest.main()
