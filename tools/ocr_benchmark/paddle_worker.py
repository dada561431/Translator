"""One isolated CPU benchmark worker per model tier, not a production helper."""
import json
import sys
import time
from importlib.metadata import version
from pathlib import Path


def reading_order(texts, boxes, scores):
    if not (len(texts) == len(boxes) == len(scores)):
        raise ValueError("Paddle result box/text/score counts differ")
    items = []
    for text, polygon, score in zip(texts, boxes, scores):
        points = polygon.tolist() if hasattr(polygon, "tolist") else polygon
        xs, ys = [p[0] for p in points], [p[1] for p in points]
        items.append(dict(text=str(text), polygon=points, score=float(score), x=min(xs),
                          y=min(ys), center=(min(ys) + max(ys)) / 2, height=max(ys) - min(ys)))
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
    # No confidence filter is added. Spaces between separate boxes remain in scoring.
    return dict(text="\n".join(" ".join(item["text"] for item in line) for line in ordered),
                box_count=len(items), scores=[item["score"] for item in flat],
                boxes=[item["polygon"] for item in flat],
                raw_box_texts=[str(text) for text in texts])


def run(request):
    import psutil
    process = psutil.Process()
    memory_before = process.memory_info().rss
    started = time.perf_counter()
    import cv2
    import numpy as np
    from paddleocr import PaddleOCR
    import_ms = (time.perf_counter() - started) * 1000
    tier = request["tier"]
    models = Path(request["models"])
    dirs = {}
    for kind in ("det", "rec"):
        name = f"PP-OCRv6_{tier}_{kind}"
        configs = list((models / name).rglob("inference.yml"))
        if len(configs) != 1:
            raise ValueError(f"Prepare local model first: {name}")
        dirs[kind] = str(configs[0].parent)
    started = time.perf_counter()
    ocr = PaddleOCR(ocr_version="PP-OCRv6", device="cpu", engine="paddle_static",
        text_detection_model_name=f"PP-OCRv6_{tier}_det", text_detection_model_dir=dirs["det"],
        text_recognition_model_name=f"PP-OCRv6_{tier}_rec", text_recognition_model_dir=dirs["rec"],
        use_doc_orientation_classify=False, use_doc_unwarping=False, use_textline_orientation=False,
        cpu_threads=request["cpu_threads"], enable_mkldnn=request["mkldnn"], text_rec_score_thresh=0.0)
    response = dict(engine=f"PP-OCRv6_{tier}", device="cpu", import_ms=import_ms,
        constructor_ms=(time.perf_counter() - started) * 1000,
        memory_before_bytes=memory_before, memory_after_constructor_bytes=process.memory_info().rss,
        cpu_threads=request["cpu_threads"], enable_mkldnn=request["mkldnn"],
        orientation_models=False, recognition_score_threshold=0.0, model_dirs=dirs,
        versions={package: version(package) for package in ("paddleocr", "paddlepaddle", "paddlex")}, rows=[])

    def predict(image):
        results = list(ocr.predict(image))
        if len(results) != 1:
            raise ValueError("Expected one image result")
        result = results[0]
        return reading_order(result["rec_texts"], result["rec_polys"], result["rec_scores"])

    for sample in request["samples"]:
        row = dict(filename=sample["filename"], text="", error="", times_ms=[], run_texts=[])
        try:
            image = cv2.imdecode(np.fromfile(sample["path"], dtype=np.uint8), cv2.IMREAD_COLOR)
            if image is None:
                raise ValueError("Image decode failed")
            row.update(input_width=int(image.shape[1]), input_height=int(image.shape[0]))
            started = time.perf_counter()
            predict(image)
            row["warmup_ms"] = (time.perf_counter() - started) * 1000
            if len(response["rows"]) == 0:
                response["cold_ready_including_first_inference_ms"] = response["constructor_ms"] + row["warmup_ms"]
                response["memory_after_ready_bytes"] = process.memory_info().rss
            for _ in range(request["repeats"]):
                started = time.perf_counter()
                output = predict(image)
                row["times_ms"].append((time.perf_counter() - started) * 1000)
                row["run_texts"].append(output["text"])
                row.update(output)
        except Exception as error:
            row["error"] = f"{type(error).__name__}: {error}"
        response["rows"].append(row)
    response["memory_after_all_bytes"] = process.memory_info().rss
    return response


if __name__ == "__main__":
    request_path, output_path = map(Path, sys.argv[1:])
    request = json.loads(request_path.read_text(encoding="utf-8"))
    try:
        response = run(request)
    except Exception as error:
        response = dict(engine=f"PP-OCRv6_{request['tier']}", startup_error=f"{type(error).__name__}: {error}",
                        rows=[dict(filename=s["filename"], text="", error=f"Startup failed: {error}", times_ms=[])
                              for s in request["samples"]])
    output_path.write_text(json.dumps(response, ensure_ascii=False, indent=2), encoding="utf-8")
