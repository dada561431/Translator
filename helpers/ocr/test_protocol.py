import io
import json
import struct
import unittest
from paddle_helper import read_header, validate_image, write_frame, reading_order, serve


class ProtocolTest(unittest.TestCase):
    def header(self):
        return dict(type="recognize", protocol=1, request_id="1", language="zh", format="RGB888",
                    width=5, height=2, stride=16, payload_bytes=32)

    def test_unicode_roundtrip(self):
        stream = io.BytesIO()
        write_frame(stream, dict(type="result", text="\u4f60\u597d\n\u4e16\u754c"))
        stream.seek(0)
        self.assertEqual(read_header(stream)["text"], "\u4f60\u597d\n\u4e16\u754c")

    def test_pixel_bounds(self):
        self.assertEqual(validate_image(self.header()), (5, 2, 16, 32))
        for key, value in [("stride", 10), ("width", -1), ("payload_bytes", 100000000), ("height", True)]:
            with self.assertRaises(ValueError):
                validate_image(dict(self.header(), **{key: value}))

    def test_truncated_and_oversized_header(self):
        for data in (struct.pack(">I", 20000), struct.pack(">I", 4) + b"{}"):
            with self.assertRaises((ValueError, EOFError)):
                read_header(io.BytesIO(data))
        self.assertIsNone(read_header(io.BytesIO()))

    def test_box_order_and_low_confidence(self):
        def box(x, y):
            return [[x, y], [x+10, y], [x+10, y+10], [x, y+10]]
        value = reading_order(["bottom", "right", "left"], [box(0, 20), box(20, 0), box(0, 0)], [.1, .9, .8])
        self.assertEqual(value["text"], "left right\nbottom")
        self.assertIn(.1, value["scores"])
        self.assertEqual(value["box_texts"], ["left", "right", "bottom"])

    def test_resident_serving(self):
        class Engine:
            calls = 0
            def recognize(self, pixels, width, height, stride):
                self.calls += 1
                return dict(text=str(self.calls), scores=[], boxes=[])
        request = io.BytesIO()
        for i in ("1", "2"):
            write_frame(request, dict(self.header(), request_id=i))
            request.write(bytes(32))
        request.seek(0)
        response, engine = io.BytesIO(), Engine()
        serve(request, response, engine)
        response.seek(0)
        self.assertEqual(read_header(response)["type"], "ready")
        self.assertEqual(read_header(response)["text"], "1")
        self.assertEqual(read_header(response)["text"], "2")
        self.assertEqual(engine.calls, 2)

    def test_inference_error_not_empty(self):
        class Engine:
            def recognize(self, *args):
                raise RuntimeError("model failure")
        request, response = io.BytesIO(), io.BytesIO()
        write_frame(request, self.header()); request.write(bytes(32)); request.seek(0)
        serve(request, response, Engine()); response.seek(0)
        read_header(response)
        result = read_header(response)
        self.assertEqual(result["type"], "error")
        self.assertEqual(result["request_id"], "1")


if __name__ == "__main__":
    unittest.main()
