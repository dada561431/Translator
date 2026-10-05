"""Build an isolated Python layout from explicit, already verified input roots."""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import shutil
import subprocess

VERSIONS = {"paddleocr": "3.7.0", "paddlepaddle": "3.3.1", "paddlex": "3.7.2"}
BUILD_ONLY = {"pip", "pyinstaller", "pyinstaller_hooks_contrib", "_pyinstaller_hooks_contrib", "altgraph",
              "pefile", "peutils", "pywin32_ctypes", "win32ctypes"}


def is_build_only(name):
    normalized = name.lower().replace("-", "_").removesuffix(".py")
    return any(normalized == tool or normalized.startswith(tool + "_") for tool in BUILD_ONLY)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def copy_tree(source, target):
    shutil.copytree(source, target, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pdb", "*.lib"))


def build_python(base, site, target):
    base, site, target = map(Path, (base, site, target))
    if target.exists():
        raise ValueError("Runtime output must not exist; cleanup belongs to the caller")
    if not (base / "python312.dll").is_file() or not (base / "Lib/encodings").is_dir():
        raise ValueError("Expected a complete Python 3.12 base, not a venv")
    distributions = list(importlib.metadata.distributions(path=[str(site)]))
    actual = {d.metadata["Name"].lower(): d.version for d in distributions}
    for name, version in VERSIONS.items():
        if actual.get(name) != version:
            raise ValueError(f"Unverified {name} version: {actual.get(name)}")
    target.mkdir(parents=True)
    for source in base.iterdir():
        if source.is_file() and (source.suffix.lower() in {".exe", ".dll"} or source.name == "LICENSE.txt"):
            if source.name != "pythonw.exe":
                shutil.copy2(source, target / source.name)
    copy_tree(base / "DLLs", target / "DLLs")
    for source in (base / "Lib").iterdir():
        if source.name in {"site-packages", "__pycache__", "test", "tests", "ensurepip", "idlelib", "tkinter"}:
            continue
        if source.is_dir():
            copy_tree(source, target / "Lib" / source.name)
        elif source.suffix != ".pyc":
            (target / "Lib").mkdir(exist_ok=True)
            shutil.copy2(source, target / "Lib" / source.name)
    output_site = target / "Lib/site-packages"
    output_site.mkdir(parents=True)
    for source in site.iterdir():
        if (source.name == "__pycache__"
                or (source.suffix == ".pth" and source.name != "distutils-precedence.pth")
                or is_build_only(source.name)):
            continue
        if source.is_dir():
            copy_tree(source, output_site / source.name)
        else:
            shutil.copy2(source, output_site / source.name)
    # _pth ignores registry, PYTHONHOME/PYTHONPATH and the user's site-packages.
    (target / "python312._pth").write_text(".\nDLLs\nLib\nLib/site-packages\nimport site\n", encoding="ascii")
    return actual


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--site", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build_python(args.base, args.site, args.output)
    subprocess.run([str(args.output / "python.exe"), "-I", "-B", "-c",
                    "import sys,paddle,paddleocr,paddlex; print(sys.version.split()[0]); "
                    "print(paddle.__version__,paddleocr.__version__,paddlex.__version__)"], check=True)


if __name__ == "__main__":
    main()
