"""Explicit local IPC acceptance tool; never invokes a translation provider."""
import argparse
import json
import os
from pathlib import Path
import queue
import statistics
import struct
import subprocess
import tempfile
import threading
import time


def smoke(program, script, models, image=None, count=20):
    from PIL import Image, ImageDraw, ImageFont
    if image:
        pixels = Image.open(image).convert("RGB")
    else:
        pixels = Image.new("RGB", (720, 110), "white")
        font = ImageFont.truetype(str(Path(os.environ["WINDIR"]) / "Fonts/arial.ttf"), 44)
        ImageDraw.Draw(pixels).text((18, 22), "Hello Portable OCR", fill="black", font=font)
    width, height = pixels.size
    data = pixels.tobytes()
    args = [str(Path(program).resolve())]
    if script:
        args += ["-B", "-u", str(Path(script).resolve())]
    args += ["--models", str(Path(models).resolve()), "--cpu-threads", "4"]
    env = {key: value for key, value in os.environ.items() if key.upper() in {
        "SYSTEMROOT", "WINDIR", "TEMP", "TMP", "USERPROFILE", "LOCALAPPDATA", "APPDATA", "NUMBER_OF_PROCESSORS"}}
    env["PATH"] = str(Path(os.environ["WINDIR"]) / "System32")
    env.update(PYTHONHOME="Z:/invalid-python", PYTHONPATH="Z:/invalid-packages",
               PYTHONNOUSERSITE="1", PYTHONDONTWRITEBYTECODE="1", HF_HUB_OFFLINE="1",
               HF_HUB_DISABLE_TELEMETRY="1", PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK="True")
    frames, logs = queue.Queue(), []
    with tempfile.TemporaryDirectory(prefix="translator-smoke-") as cache:
        env.update(PADDLE_PDX_CACHE_HOME=cache, HF_HOME=cache, PADDLE_HOME=cache, MODELSCOPE_CACHE=cache)
        started = time.perf_counter()
        process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, cwd=str(Path(program).resolve().parent), env=env,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        def read_frames():
            try:
                while True:
                    prefix = process.stdout.read(4)
                    if len(prefix) != 4:
                        raise EOFError("Helper exited before a complete frame")
                    size = struct.unpack(">I", prefix)[0]
                    if not 0 < size <= 4 * 1024 * 1024:
                        raise ValueError("Invalid frame size")
                    body = process.stdout.read(size)
                    frames.put(json.loads(body))
            except Exception as error:
                frames.put(error)
        def read_logs():
            while block := process.stderr.read(1024):
                logs.append(block.decode("utf-8", errors="replace"))
                if len(logs) > 32:
                    logs.pop(0)
        reader = threading.Thread(target=read_frames, daemon=True)
        logger = threading.Thread(target=read_logs, daemon=True)
        reader.start()
        logger.start()
        def reply(timeout):
            frame = frames.get(timeout=timeout)
            if isinstance(frame, Exception):
                raise RuntimeError(str(frame) + ": " + "".join(logs)[-4000:])
            return frame
        try:
            ready = reply(60)
            if ready.get("type") != "ready" or ready.get("protocol") != 1 or ready.get("mkldnn") is not False:
                raise ValueError(f"Invalid READY: {ready}")
            ready_ms = (time.perf_counter() - started) * 1000
            latencies, recognized = [], []
            for index in range(count):
                header = dict(type="recognize", protocol=1, request_id=str(index), language="en",
                              width=width, height=height, stride=width * 3, payload_bytes=len(data), format="RGB888")
                encoded = json.dumps(header).encode()
                call = time.perf_counter()
                process.stdin.write(struct.pack(">I", len(encoded)) + encoded + data)
                process.stdin.flush()
                result = reply(15)
                if result.get("type") != "result" or result.get("request_id") != str(index):
                    raise ValueError(f"Invalid result: {result}")
                if not result.get("text", "").strip():
                    raise ValueError("Unexpected empty OCR result")
                if not image and "helloportableocr" not in "".join(result["text"].lower().split()):
                    raise ValueError("Synthetic smoke text was not recognized")
                latencies.append((time.perf_counter() - call) * 1000)
                recognized.append(result["text"])
            warm = latencies[1:]
            report = dict(ready_ms=ready_ms, first_ms=latencies[0], count=count, pid=process.pid,
                          warm_median_ms=statistics.median(warm), warm_mean_ms=statistics.mean(warm),
                          warm_over_300=sum(value > 300 for value in warm), mkldnn=False,
                          text=recognized[-1], minimal_path=True, python_environment_polluted=True)
        finally:
            process.kill()
            process.wait(timeout=5)
            reader.join(timeout=2)
            logger.join(timeout=2)
        report["cleanup"] = process.poll() is not None
        report["stderr_mkldnn_disabled"] = "MKL-DNN disabled" in "".join(logs)
        return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", required=True)
    parser.add_argument("--script")
    parser.add_argument("--models", required=True)
    parser.add_argument("--image")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = smoke(args.program, args.script, args.models, args.image)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=True))


if __name__ == "__main__":
    main()
