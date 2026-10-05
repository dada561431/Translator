"""Resident local PP-OCRv6-small CPU worker. stdout is a framed binary protocol."""
import argparse
import hashlib
import json
import os
import struct
import sys
import time
from pathlib import Path

PROTOCOL = 1
MAX_HEADER = 16384
MAX_PIXELS = 64 * 1024 * 1024


def read_exact(stream, count):
    data = bytearray()
    while len(data) < count:
        block = stream.read(count - len(data))
        if not block:
            if not data:
                return None
            raise EOFError("Truncated IPC frame")
        data.extend(block)
    return bytes(data)


def read_header(stream):
    prefix = read_exact(stream, 4)
    if prefix is None:
        return None
    length = struct.unpack(">I", prefix)[0]
    if not 0 < length <= MAX_HEADER:
        raise ValueError("Invalid IPC header size")
    payload = read_exact(stream, length)
    if payload is None:
        raise EOFError("Missing IPC header")
    header = json.loads(payload)
    if not isinstance(header, dict) or header.get("protocol") != PROTOCOL:
        raise ValueError("Invalid IPC protocol")
    return header


def write_frame(stream, response):
    payload = json.dumps(dict(response, protocol=PROTOCOL), ensure_ascii=False, allow_nan=False).encode("utf-8")
    if len(payload) > 4 * 1024 * 1024:
        raise ValueError("IPC response too large")
    stream.write(struct.pack(">I", len(payload)) + payload)
    stream.flush()


def validate_image(header):
    if header.get("type") != "recognize" or header.get("format") != "RGB888":
        raise ValueError("Invalid IPC request type/format")
    if not isinstance(header.get("request_id"), str) or not header["request_id"] or len(header["request_id"]) > 64:
        raise ValueError("Invalid request identity")
    if header.get("language") not in {"auto", "zh", "en", "ja"}:
        raise ValueError("Unsupported language")
    width, height, stride, size = (header.get(key) for key in ("width", "height", "stride", "payload_bytes"))
    if any(type(value) is not int for value in (width, height, stride, size)):
        raise ValueError("Invalid pixel metadata")
    if not (0 < width <= 16384 and 0 < height <= 16384 and width * 3 <= stride <= width * 3 + 3
            and size == height * stride and 0 < size <= MAX_PIXELS):
        raise ValueError("Invalid pixel bounds/stride")
    return width, height, stride, size


def native_model_directory(source, cache):
    """Paddle's Windows narrow-path reader cannot open every Unicode model path."""
    source = Path(source).resolve()
    if os.name != "nt" or str(source).isascii():
        return str(source)
    import ctypes
    import shutil
    import tempfile
    cache = Path(cache).resolve()
    cache.mkdir(parents=True, exist_ok=True)
    if not str(cache).isascii():
        buffer = ctypes.create_unicode_buffer(32768)
        get_short = ctypes.windll.kernel32.GetShortPathNameW
        get_short.argtypes = (ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint32)
        get_short.restype = ctypes.c_uint32
        length = get_short(str(cache), buffer, len(buffer))
        if not length or length >= len(buffer) or not buffer.value.isascii():
            raise RuntimeError("Paddle requires an ASCII native model-cache path; this Unicode user cache has no ASCII short-path alias")
        cache = Path(buffer.value)
    files = ("inference.yml", "inference.json", "inference.pdiparams")
    hashes = {}
    for name in files:
        with (source / name).open("rb") as stream:
            hashes[name] = hashlib.file_digest(stream, "sha256").hexdigest()
    identity = hashlib.sha256(json.dumps(hashes, sort_keys=True).encode("ascii")).hexdigest()
    destination = cache / "verified-local-models" / identity
    destination.mkdir(parents=True, exist_ok=True)
    for name, expected in hashes.items():
        target = destination / name
        if target.is_file():
            with target.open("rb") as stream:
                if hashlib.file_digest(stream, "sha256").hexdigest() == expected:
                    continue
        with tempfile.NamedTemporaryFile(dir=destination, delete=False) as temp:
            temporary = Path(temp.name)
        try:
            shutil.copyfile(source / name, temporary)
            with temporary.open("rb") as stream:
                if hashlib.file_digest(stream, "sha256").hexdigest() != expected:
                    raise RuntimeError("Local model changed while staging")
            os.replace(temporary, target)
        finally:
            temporary.unlink(missing_ok=True)
    print(f"Verified bundled model copy; identity={identity[:16]}", file=sys.stderr, flush=True)
    return str(destination)


def reading_order(texts, boxes, scores):
    if not (len(texts) == len(boxes) == len(scores)):
        raise ValueError("OCR text/box/score counts differ")
    items = []
    for text, polygon, score in zip(texts, boxes, scores):
        points = polygon.tolist() if hasattr(polygon, "tolist") else polygon
        xs, ys = [p[0] for p in points], [p[1] for p in points]
        items.append(dict(text=str(text), polygon=points, score=float(score), x=min(xs),
            center=(min(ys) + max(ys)) / 2, height=max(ys) - min(ys)))
    lines = []
    for item in sorted(items, key=lambda item: (item["center"], item["x"])):
        line = next((line for line in lines if abs(line[0]["center"] - item["center"])
            <= .5 * max(1, min(line[0]["height"], item["height"]))), None)
        if line is None:
            lines.append([item])
        else:
            line.append(item)
    ordered = [sorted(line, key=lambda item: item["x"]) for line in lines]
    flat = [item for line in ordered for item in line]
    return dict(text="\n".join(" ".join(item["text"] for item in line) for line in ordered),
        scores=[item["score"] for item in flat], boxes=[item["polygon"] for item in flat],
        box_texts=[item["text"] for item in flat])


class SmallOcr:
    def __init__(self, models, cpu_threads):
        self.model_names = ["PP-OCRv6_small_det", "PP-OCRv6_small_rec"]
        directories = {}
        # Validate all local assets before importing the inference stack; never download.
        for kind in ("det", "rec"):
            name = f"PP-OCRv6_small_{kind}"
            configs = list((Path(models) / name).rglob("inference.yml"))
            if len(configs) != 1 or not (configs[0].parent / "inference.json").is_file():
                raise FileNotFoundError(f"Local model missing or ambiguous: {name}")
            if not (configs[0].parent / "inference.pdiparams").is_file():
                raise FileNotFoundError(f"Local weights missing: {name}")
            directories[kind] = native_model_directory(configs[0].parent,
                os.environ.get("PADDLE_PDX_CACHE_HOME", str(Path.home() / ".cache/translator-ocr")))
        print("MKL-DNN disabled; PP-OCRv6-small CPU; loading local models", file=sys.stderr, flush=True)
        from paddleocr import PaddleOCR
        self.ocr = PaddleOCR(device="cpu", engine="paddle_static",
            text_detection_model_name="PP-OCRv6_small_det", text_detection_model_dir=directories["det"],
            text_recognition_model_name="PP-OCRv6_small_rec", text_recognition_model_dir=directories["rec"],
            use_doc_orientation_classify=False, use_doc_unwarping=False, use_textline_orientation=False,
            cpu_threads=cpu_threads, enable_mkldnn=False, text_rec_score_thresh=0.0)

    def recognize(self, pixels, width, height, stride):
        import numpy as np
        # QImage RGB888 rows can have alignment padding; Paddle expects contiguous BGR.
        rgb = np.frombuffer(pixels, dtype=np.uint8).reshape(height, stride)[:, :width * 3].reshape(height, width, 3)
        results = list(self.ocr.predict(rgb[:, :, ::-1].copy()))
        if len(results) != 1:
            raise ValueError("Expected one OCR image result")
        result = results[0]
        ordered = reading_order(result["rec_texts"], result["rec_polys"], result["rec_scores"])
        # The installed PaddleX OCR pipeline propagates polygons, not detection scores.
        ordered["detection_score_status"] = "not exposed by current result path"
        return ordered


def serve(input_stream, output_stream, engine):
    write_frame(output_stream, dict(type="ready", engine="paddle-small", mkldnn=False,
                                   models=getattr(engine, "model_names", [])))
    while True:
        header = read_header(input_stream)
        if header is None or header.get("type") == "shutdown":
            return
        width, height, stride, size = validate_image(header)
        pixels = read_exact(input_stream, size)
        if pixels is None:
            raise EOFError("Missing IPC image pixels")
        started = time.perf_counter()
        try:
            result = engine.recognize(pixels, width, height, stride)
            write_frame(output_stream, dict(result, type="result", request_id=header["request_id"],
                elapsed_ms=(time.perf_counter() - started) * 1000))
        except Exception as error:
            # Do not include frame text/pixels in diagnostic logs.
            print(f"OCR inference error: {type(error).__name__}", file=sys.stderr, flush=True)
            write_frame(output_stream, dict(type="error", request_id=header["request_id"],
                error=f"{type(error).__name__}: {str(error)[:500]}"))
            return


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--models", required=True)
    parser.add_argument("--cpu-threads", type=int, default=4)
    args = parser.parse_args()
    if os.name == "nt":
        import msvcrt
        msvcrt.setmode(sys.stdin.fileno(), os.O_BINARY)
        msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)
    # Preserve the pipe for protocol replies, then redirect even native stdout logs.
    output = os.fdopen(os.dup(sys.stdout.fileno()), "wb", buffering=0)
    os.dup2(sys.stderr.fileno(), sys.stdout.fileno())
    try:
        engine = SmallOcr(Path(args.models), max(1, min(args.cpu_threads, 16)))
        print("PP-OCRv6-small ready; MKL-DNN disabled", file=sys.stderr, flush=True)
        serve(sys.stdin.buffer, output, engine)
    except Exception as error:
        print(f"Helper error: {type(error).__name__}: {str(error)[:500]}", file=sys.stderr, flush=True)
        try:
            write_frame(output, dict(type="error", error=f"{type(error).__name__}: {str(error)[:500]}"))
        except (OSError, ValueError):
            pass
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
