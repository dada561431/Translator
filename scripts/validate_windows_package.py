"""Fail-closed manifest checks and explicit opt-in local runtime smoke."""
import argparse
import json
import os
from pathlib import Path, PureWindowsPath
import re
import subprocess
import tempfile
from portable_runtime import VERSIONS, digest

REQUIRED = ("Translator.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Network.dll",
            "platforms/qwindows.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll",
            "ocr/runtime/python.exe", "ocr/runtime/python312.dll", "ocr/runtime/python312._pth",
            "ocr/helper/paddle_helper.py", "licenses/python/LICENSE.txt", "licenses/component-matrix.json")


def relative_file(root, name):
    if not isinstance(name, str) or not name or "\\" in name or PureWindowsPath(name).drive:
        raise ValueError("Manifest contains an absolute or noncanonical path")
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file():
        raise ValueError(f"Missing/unsafe package file: {name}")
    return path


def validate(root, hashes=True):
    root = Path(root).resolve()
    manifest = json.loads((root / "runtime-manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("mkldnn") is not False:
        raise ValueError("Invalid manifest schema or MKL-DNN setting")
    if manifest.get("versions", {}).get("qt") != "6.11.2" or manifest["versions"].get("python") != "3.12.14":
        raise ValueError("Unverified Qt/Python version")
    for key, value in VERSIONS.items():
        if manifest["versions"].get(key) != value:
            raise ValueError(f"Unverified {key} version")
    serialized = json.dumps(manifest)
    if re.search(r"[A-Za-z]:[\\/]|C:\\\\Users|D:\\\\workspace|sk-[A-Za-z0-9]{12}|ghp_[A-Za-z0-9]{12}", serialized):
        raise ValueError("Manifest contains a forbidden path or credential")
    paths = set()
    for entry in manifest.get("files", []):
        name = entry["path"]
        if name in paths:
            raise ValueError("Duplicate manifest path")
        paths.add(name)
        path = relative_file(root, name)
        if path.stat().st_size != entry["bytes"] or (hashes and digest(path) != entry["sha256"]):
            raise ValueError(f"Manifest mismatch: {name}")
    for name in REQUIRED:
        relative_file(root, name)
        if name not in paths:
            raise ValueError(f"Required file not in manifest: {name}")
    for kind in ("det", "rec"):
        name = f"PP-OCRv6_small_{kind}"
        for file in ("inference.yml", "inference.json", "inference.pdiparams"):
            item = f"ocr/models/{name}/{name}_infer/{file}"
            relative_file(root, item)
            if item not in paths:
                raise ValueError("Model not in manifest")
    actual = {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()}
    if actual != paths | {"runtime-manifest.json"}:
        raise ValueError("Unmanifested or missing package files")
    if any(p.endswith((".pdb", ".pyc", ".log")) or "pyvenv.cfg" in p or "private" in Path(p).parts for p in actual):
        raise ValueError("Development artifact found")
    return manifest


def dependency_audit(root):
    import pefile
    root = Path(root).resolve()
    system = Path(os.environ["WINDIR"]) / "System32"
    binaries = [p for p in root.rglob("*") if p.suffix.lower() in {".exe", ".dll", ".pyd"}]
    bundled = {p.name.lower() for p in binaries}
    missing, records = [], []
    # Candidate-presence audit, not a proof of Windows loader resolution or clean-machine execution.
    for path in binaries:
        pe = pefile.PE(str(path), fast_load=True)
        pe.parse_data_directories(directories=[1, 13])
        for entry in list(getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])) + list(getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", [])):
            dll = entry.dll.decode("ascii").lower()
            microsoft_crt = dll.startswith(("vcruntime", "msvcp", "concrt", "vcomp"))
            known_system = dll.startswith(("api-ms-win-", "ext-ms-win-")) or ((system / dll).is_file() and not microsoft_crt)
            found = dll in bundled or known_system
            record = {"importer": path.relative_to(root).as_posix(), "dependency": dll,
                      "candidate_present": found, "source": "bundled" if dll in bundled else "windows" if known_system else "missing"}
            records.append(record)
            if not found:
                missing.append(record)
        pe.close()
    return {"binary_count": len(binaries), "missing_count": len(missing), "missing": missing,
            "imports": records, "scope": "static candidate presence; dynamic loader and clean-machine acceptance required"}


def validate_model_hashes(root, catalog=None):
    if catalog is None:
        catalog = json.loads(Path(__file__).with_name("runtime-models.json").read_text(encoding="utf-8"))
    for entry in catalog["files"]:
        model = relative_file(Path(root) / "ocr/models", entry["path"])
        if model.stat().st_size != entry["bytes"] or digest(model) != entry["sha256"]:
            raise ValueError("Model does not match the verified Phase 6.1A catalog")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--audit-output", type=Path)
    args = parser.parse_args()
    manifest = validate(args.package)
    validate_model_hashes(args.package)
    if args.audit_output:
        report = dependency_audit(args.package)
        args.audit_output.parent.mkdir(parents=True, exist_ok=True)
        args.audit_output.write_text(json.dumps(report, indent=2), encoding="utf-8")
        if report["missing_count"]:
            raise ValueError(f"Missing native dependency candidates: {report['missing_count']}")
    if args.smoke:
        env = {k: v for k, v in os.environ.items() if k.upper() in {
            "WINDIR", "SYSTEMROOT", "LOCALAPPDATA", "APPDATA", "USERPROFILE", "TEMP", "TMP"}}
        env.update(PATH=str(Path(os.environ["WINDIR"]) / "System32"),
                   PYTHONHOME="Z:/invalid", PYTHONPATH="Z:/invalid",
                   QT_PLUGIN_PATH="Z:/invalid", QT_QPA_PLATFORM_PLUGIN_PATH="Z:/invalid")
        with tempfile.TemporaryDirectory(prefix="translator-validation-") as temp:
            report_path = Path(temp) / "self-check.json"
            result = subprocess.run([str(args.package.resolve() / "Translator.exe"), "--self-check", "--report", str(report_path)],
                                    env=env, cwd=temp, timeout=120, capture_output=True)
            if result.returncode or not report_path.is_file():
                raise ValueError("Native self-check failed: " + result.stderr.decode(errors="replace")[-2000:])
            report = json.loads(report_path.read_text(encoding="utf-8"))
            if not report["success"] or not report["qwindows"]:
                raise ValueError("Native self-check did not pass")
            # The production engine destructor must reap its actual helper PID.
            import ctypes
            from ctypes import wintypes
            kernel = ctypes.windll.kernel32
            kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            kernel.OpenProcess.restype = wintypes.HANDLE
            kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            kernel.CloseHandle.restype = wintypes.BOOL
            handle = kernel.OpenProcess(0x1000, False, report["helper_pid"])
            if handle:
                kernel.CloseHandle(handle)
                raise ValueError("Helper remains after native application exit")
            print(json.dumps(report))
        validate(args.package)
    print(f"Package validated: {len(manifest['files'])} manifest files")


if __name__ == "__main__":
    main()
