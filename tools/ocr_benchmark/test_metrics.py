import csv
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

from metrics import edit_distance, evaluate, load_manifest, normalize, summarize, write_reports
from paddle_worker import reading_order
from run_benchmark import execute


class BenchmarkTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        self.sample = dict(filename="001.png", language="zh", ground_truth="\u4f60\u597d", category="outline")
        (self.root / "001.png").touch()

    def manifest(self, rows=None):
        path = self.root / "manifest.csv"
        with path.open("w", encoding="utf-8-sig", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(self.sample))
            writer.writeheader()
            writer.writerows(rows if rows is not None else [self.sample])
        return path

    def test_manifest_unicode(self):
        loaded = load_manifest(self.manifest(), self.root)
        self.assertEqual(loaded[0]["ground_truth"], "\u4f60\u597d")

    def test_missing_manifest(self):
        with self.assertRaisesRegex(ValueError, "BLOCKED"):
            load_manifest(self.root / "missing.csv", self.root)

    def test_missing_image(self):
        with self.assertRaisesRegex(ValueError, "Missing image"):
            load_manifest(self.manifest([dict(self.sample, filename="missing.png")]), self.root)

    def test_empty_truth(self):
        with self.assertRaisesRegex(ValueError, "Empty ground"):
            load_manifest(self.manifest([dict(self.sample, ground_truth=" \r\n ")]), self.root)

    def test_path_escape(self):
        with self.assertRaisesRegex(ValueError, "Unsafe"):
            load_manifest(self.manifest([dict(self.sample, filename="../outside.png")]), self.root)

    def test_duplicate(self):
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            load_manifest(self.manifest([self.sample, self.sample]), self.root)

    def test_empty_manifest(self):
        with self.assertRaisesRegex(ValueError, "no samples"):
            load_manifest(self.manifest([]), self.root)

    def test_duplicate_column(self):
        path = self.root / "bad.csv"
        path.write_text("filename,language,ground_truth,category,filename\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "columns"):
            load_manifest(path, self.root)

    def test_multiline_unicode_csv(self):
        truth = "\u4f60\u597d,\"\u670b\u53cb\"\n\u7b2c\u4e8c\u884c"
        self.assertEqual(load_manifest(self.manifest([dict(self.sample, ground_truth=truth)]), self.root)[0]["ground_truth"], truth)

    def test_normalize(self):
        self.assertEqual(normalize(" \u4f60  \u597d\r\n next\rline "), "\u4f60 \u597d\nnext\nline")
        self.assertNotEqual(normalize("\u4f60\u597d\uff01"), normalize("\u4f60\u597d!"))
        self.assertNotEqual(normalize("\u570b"), normalize("\u56fd"))

    def test_edit_distance(self):
        self.assertEqual(edit_distance("kitten", "sitting"), 3)
        self.assertEqual(edit_distance("", "\u4f60\u597d"), 2)
        self.assertEqual(edit_distance("\u4f60\u597d", "\u4f60\u5417"), 1)

    def test_cer_and_failure_denominator(self):
        a = evaluate(self.sample, dict(text="\u4f60\u597d", times_ms=[3, 1, 2]), "a")
        b = evaluate(self.sample, dict(text="\u4f60\u5417", times_ms=[2, 3, 4]), "a")
        c = evaluate(self.sample, dict(text="", error="model unavailable", times_ms=[]), "a")
        summary = summarize([a, b, c])
        self.assertEqual(summary["count"], 3)
        self.assertEqual(summary["micro_cer"], .5)
        self.assertEqual(summary["counts"]["ERROR"], 1)
        self.assertEqual(a["median_ms"], 2)
        self.assertIsNone(summary["warm_sample_p95_ms"])

    def test_empty_vs_error(self):
        row = evaluate(self.sample, dict(text="", times_ms=[1, 1, 1]), "a")
        self.assertEqual(row["classification"], "EMPTY")
        self.assertEqual(row["cer"], 1)

    def test_invalid_timing(self):
        with self.assertRaises(ValueError):
            evaluate(self.sample, dict(text="", times_ms=[float("nan")]), "a")

    def test_serialization(self):
        row = evaluate(self.sample, dict(text="\u4f60\u597d", times_ms=[1, 2, 3]), "a")
        report = dict(status="TEST", samples=[self.sample], rows=[row], engines={"a": {}},
                      summaries={"a": summarize([row])})
        write_reports(self.root, report)
        self.assertEqual(json.loads((self.root / "latest.json").read_text(encoding="utf-8"))["rows"][0]["text"],
                         "\u4f60\u597d")
        with (self.root / "latest.csv").open(encoding="utf-8-sig", newline="") as stream:
            self.assertEqual(next(csv.DictReader(stream))["ground_truth"], "\u4f60\u597d")
        self.assertIn("Same-input", (self.root / "latest.md").read_text(encoding="utf-8"))

    def test_reading_order(self):
        def box(x, y):
            return [[x, y], [x + 10, y], [x + 10, y + 10], [x, y + 10]]
        result = reading_order(["bottom", "right", "left"], [box(0, 20), box(20, 0), box(0, 0)], [.1, .2, .3])
        self.assertEqual(result["text"], "left right\nbottom")
        self.assertEqual(result["box_count"], 3)
        self.assertIn(.1, result["scores"])

    def test_engine_failure(self):
        result = execute([str(self.root / "does-not-exist.exe")], dict(samples=[self.sample]),
                         self.root, "failure", os.environ.copy(), 2)
        self.assertTrue(result["rows"][0]["error"])
        self.assertEqual(len(result["rows"]), 1)

    def test_stale_response_not_used(self):
        (self.root / "stale.response.json").write_text('{"rows": []}', encoding="utf-8")
        result = execute([sys.executable, "-c", "import sys;sys.exit(1)"], dict(samples=[self.sample]),
                         self.root, "stale", os.environ.copy(), 2)
        self.assertTrue(result["startup_error"])


if __name__ == "__main__":
    unittest.main()
