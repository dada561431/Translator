import unittest
from analysis import analyze, distribution, reassemble, summarize


def box(text, score, x=0, y=0):
    return dict(text=text, score=score, box=[[x,y],[x+10,y],[x+10,y+10],[x,y+10]],
                relative_width=.1, relative_height=.2, relative_area=.02,
                relative_center_x=.5, relative_center_y=.5)


def result(boxes, kind="positive", truth=""):
    return dict(type=kind, ground_truth=truth, error="", box_texts_available=True, results=boxes)


class AnalysisTest(unittest.TestCase):
    def test_mixed_boxes_not_average(self):
        value = result([box("good", .96), box("noise", .22, 30)])
        self.assertEqual(reassemble(value, .5), ("good", 1, 1))

    def test_all_filtered_success_empty(self):
        self.assertEqual(reassemble(result([box("noise", .2)]), .5), ("", 0, 1))

    def test_short_words_not_length_filtered(self):
        for text in ("\u597d", "\u55ef", "\u5bf9", "\u662f", "\u8d70", "\u6ca1\u4e8b"):
            self.assertEqual(reassemble(result([box(text, .9)]), .5)[0], text)

    def test_order_and_lines(self):
        value = result([box("bottom", .9, 0, 20), box("right", .9, 30), box("left", .9)])
        self.assertEqual(reassemble(value, .5)[0], "left right\nbottom")

    def test_missing_metadata_never_guessed(self):
        with self.assertRaises(ValueError):
            reassemble(dict(error="", box_texts_available=False, text="unseparated"))

    def test_failed_negative_not_claimed_suppressed(self):
        value = result([], "negative"); value["error"] = "timeout"
        summary = summarize([value])
        self.assertEqual(summary["negative_errors"], 1)
        self.assertIsNone(summary["negative_fp_frame_rate"])

    def test_sweep_reports_positive_loss(self):
        values = [result([box("real", .6)], truth="real"), result([box("noise", .3)], "negative")]
        report = analyze(values)
        self.assertEqual(report["raw"]["negative_fp"], 1)
        at7 = next(r for r in report["confidence_sweep"] if r["threshold"] == .7)
        self.assertEqual(at7["positive_rejected"], 1)
        self.assertEqual(at7["positive_cer"], 1)

    def test_score_buckets(self):
        value = distribution([0, .2, .4, .6, .8, 1], True)
        self.assertEqual(sum(value["buckets"]), 6)
        self.assertEqual(value["buckets"], [1, 1, 1, 1, 2])


if __name__ == "__main__":
    unittest.main()
