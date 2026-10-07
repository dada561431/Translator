import unittest

from analyze_streaming_asr_probe import analyze


class StreamingAnalysisTest(unittest.TestCase):
    def test_flush_and_cues_are_not_streaming_speech_evidence(self):
        events = [
            {"event": "cue", "wall_ms": 0},
            {"event": "partial", "wall_ms": 500, "audio_ms": 480, "text": "hello", "segment": 0},
            {"event": "partial", "wall_ms": 1100, "audio_ms": 1080, "text": "hello world", "segment": 0},
            {"event": "partial", "wall_ms": 1800, "audio_ms": 1500, "text": "hello world!", "during_flush": True},
        ]
        result = analyze(events)
        self.assertEqual(result["nonempty_streaming_partial_count"], 2)
        self.assertIsNone(result["speech_start_to_first_partial_ms"])
        self.assertIsNone(result["partials_before_annotated_speech_end"])
        result = analyze(events, 100, 1200)
        self.assertEqual(result["speech_start_to_first_partial_ms"], 400)
        self.assertEqual(result["partials_before_annotated_speech_end"], 2)
        self.assertEqual(result["within_segment_interval_median_ms"], 600)

    def test_no_partial_is_unknown_not_zero_latency(self):
        result = analyze([])
        self.assertIsNone(result["audio_start_to_first_partial_ms"])
        self.assertIsNone(result["partial_interval_median_ms"])
        self.assertEqual(result["model_load_events"], 0)

    def test_accuracy_never_corrects_or_uses_output_as_truth(self):
        events = [{"event": "final", "text": "Hello, word!"}]
        result = analyze(events, ground_truth="Hello world.")
        self.assertFalse(result["accuracy"]["normalized_exact_match"])
        self.assertEqual(result["accuracy"]["wer"], 0.5)
        self.assertAlmostEqual(result["accuracy"]["cer"], 0.1)
        self.assertNotIn("accuracy", analyze(events))


if __name__ == "__main__":
    unittest.main()
