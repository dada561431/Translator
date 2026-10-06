"""Windows x64 draft portable builder. Inputs are explicit; never searches user caches."""
import argparse
import importlib.metadata
import json
import os
from pathlib import Path
import shutil
import subprocess
from portable_runtime import VERSIONS, build_python, copy_tree, digest
from validate_windows_package import validate, dependency_audit
from vc_runtime_inputs import load_vc_inputs

ROOT = Path(__file__).resolve().parents[1]


def run(args, env=None):
    subprocess.run(list(map(str, args)), cwd=ROOT, env=env, check=True)


def collect_licenses(runtime, python_base, qt, mingw, output):
    matrix = []
    site = runtime / "Lib/site-packages"
    for dist in sorted(importlib.metadata.distributions(path=[str(site)]), key=lambda d: d.metadata["Name"].lower()):
        name = dist.metadata["Name"]
        target = output / "python-packages" / name
        files = []
        for item in dist.files or []:
            source = Path(dist.locate_file(item))
            if (any(token in str(item).lower() for token in ("license", "licence", "copying", "notice"))
                    and source.is_file()):
                if not source.resolve().is_relative_to(runtime.resolve()):
                    continue
                destination = target / Path(str(item))
                if not destination.resolve().is_relative_to(output.resolve()):
                    raise ValueError("Unsafe license metadata path")
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
                files.append(destination.relative_to(output).as_posix())
        classifiers = dist.metadata.get_all("Classifier", [])
        license_name = dist.metadata.get("License-Expression") or next((c.split(" :: ")[-1] for c in classifiers
            if c.startswith("License ::")), None) or (dist.metadata.get("License") or "NOASSERTION").splitlines()[0]
        urls = dist.metadata.get_all("Project-URL", [])
        url = next((u.partition(",")[2].strip() for u in urls if any(k in u.lower() for k in ("source", "homepage", "repository"))),
                   dist.metadata.get("Home-page", ""))
        matrix.append(dict(component=name, version=dist.version, license=license_name, distributed=True,
                           source_url=url, notice_files=files, review_status="REVIEW REQUIRED",
                           review="retain upstream notices; binary/transitive obligations require review"))
    (output / "python").mkdir(parents=True)
    shutil.copy2(python_base / "LICENSE.txt", output / "python/LICENSE.txt")
    copy_tree(mingw / "licenses/gcc", output / "mingw/gcc")
    copy_tree(mingw / "licenses/mingw-w64", output / "mingw/mingw-w64")
    copy_tree(mingw / "licenses/winpthreads", output / "mingw/winpthreads")
    # Preserve the actual SDK's source SBOM, including bundled third-party provenance.
    (output / "qt").mkdir(parents=True)
    for name in ("qtbase", "qtsvg"):
        for suffix in ("source.spdx", "cdx.json"):
            source = qt / "sbom" / f"{name}-6.11.2.{suffix}"
            if source.is_file():
                shutil.copy2(source, output / "qt" / source.name)
    matrix.extend([
        dict(component="CPython", version="3.12.14", license="PSF-2.0 and included notices", distributed=True,
             source_url="https://www.python.org/", notice_files=["python/LICENSE.txt"]),
        dict(component="Qt Core/Gui/Widgets/Network/Svg and deployed plugins", version="6.11.2",
             license="LGPL-3.0/GPL/commercial alternatives; consult source SBOM", distributed=True,
             source_url="https://doc.qt.io/qt-6/licensing.html", notice_files=["qt/qtbase-6.11.2.source.spdx"],
             review="owner must confirm Qt acquisition and applicable license; source/relinking/third-party notices review pending"),
        dict(component="MinGW runtime", version="13.1", license="GPL-3.0 with GCC runtime exception; mingw-w64/winpthreads notices",
             distributed=True, source_url="https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html", notice_files=["mingw/gcc/COPYING.RUNTIME"]),
        dict(component="PP-OCRv6 small det/rec", version="Phase 6.1A verified assets", license="Apache-2.0 (official model cards)",
             distributed=True, source_url="https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det", notice_files=[], review="model notice collection requires supplied supplementary inputs")])
    for entry in matrix:
        entry.setdefault("review_status", "OWNER REVIEW REQUIRED" if entry["component"].startswith("Qt ") else "REVIEW REQUIRED")
        entry.setdefault("review", "Retain notices; complete owner/transitive redistribution review before release")
    (output / "component-matrix.json").write_text(json.dumps(matrix, indent=2, ensure_ascii=True), encoding="utf-8")
    return matrix


def main():
    parser = argparse.ArgumentParser()
    for name in ("qt-root", "mingw-root", "cmake", "ninja", "python-base", "site-packages", "model-root"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--license-root", type=Path, help="Supplementary model notices pinned by runtime-notices.json")
    parser.add_argument("--vc-runtime-root", type=Path, help="Hash-pinned official VC runtime input; owner redistribution review remains required")
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--clean-build", action="store_true", help="Recreate only the fixed task-owned portable Release build tree")
    args = parser.parse_args()
    qt, mingw = args.qt_root.resolve(), args.mingw_root.resolve()
    python_base, site, models = args.python_base.resolve(), args.site_packages.resolve(), args.model_root.resolve()
    catalog = json.loads((ROOT / "scripts/runtime-models.json").read_text(encoding="utf-8"))
    from validate_windows_package import relative_file
    for entry in catalog["files"]:
        path = relative_file(models, entry["path"])
        if digest(path) != entry["sha256"] or path.stat().st_size != entry["bytes"]:
            raise ValueError("Model inputs do not match Phase 6.1A verified assets")
    python_version = subprocess.check_output([str(python_base / "python.exe"), "-I", "-c",
                                            "import platform; print(platform.python_version())"], text=True).strip()
    if python_version != "3.12.14":
        raise ValueError("Unverified base Python version")
    env = os.environ.copy()
    env["PATH"] = str(mingw / "bin") + os.pathsep + str(qt / "bin") + os.pathsep + env.get("PATH", "")
    qt_version = subprocess.check_output([str(qt / "bin/qmake.exe"), "-query", "QT_VERSION"], env=env, text=True).strip()
    compiler_version = subprocess.check_output([str(mingw / "bin/g++.exe"), "-dumpfullversion"], env=env, text=True).strip()
    if qt_version != "6.11.2" or compiler_version != "13.1.0":
        raise ValueError("Unverified Qt/MinGW version")
    build = ROOT / "build/portable-release"
    if args.clean_build and build.exists():
        if build.resolve() != build or (ROOT / "build").is_symlink():
            raise ValueError("Unsafe clean build destination")
        shutil.rmtree(build)
    run([args.cmake, "-S", ROOT, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
         "-DCMAKE_PREFIX_PATH=" + str(qt), "-DCMAKE_CXX_COMPILER=" + str(mingw / "bin/g++.exe"),
         "-DCMAKE_MAKE_PROGRAM=" + str(args.ninja.resolve())], env)
    run([args.cmake, "--build", build, "-j", "4"], env)
    output = (ROOT / "dist/TranslatorPortable").resolve()
    # Never accept an arbitrary cleanup target, symlink/junction, or external destination.
    if output != ROOT / "dist/TranslatorPortable" or (ROOT / "dist").is_symlink():
        raise ValueError("Unsafe distribution destination")
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    shutil.copy2(build / "Translator.exe", output / "Translator.exe")
    run([qt / "bin/windeployqt.exe", "--release", "--no-translations", "--no-system-d3d-compiler",
         "--no-opengl-sw", "--no-quick-import", "--dir", output, output / "Translator.exe"], env)
    for name in ("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll"):
        shutil.copy2(mingw / "bin" / name, output / name)
    (output / "qt.conf").write_text("[Paths]\nPrefix=.\nPlugins=.\n", encoding="ascii")
    runtime = output / "ocr/runtime"
    build_python(python_base, site, runtime)
    vc_component = None
    if args.vc_runtime_root:
        vc_root = args.vc_runtime_root.resolve()
        provenance = load_vc_inputs(vc_root)
        from validate_windows_package import relative_file
        license_file = relative_file(vc_root, provenance["license_file"])
        for entry in provenance["files"]:
            source = relative_file(vc_root, entry["path"])
            if source.suffix.lower() != ".dll" or not source.name.lower().startswith(("msvcp", "vcruntime", "concrt", "vcomp")):
                raise ValueError("Unexpected VC runtime file")
            if digest(source) != entry["sha256"]:
                raise ValueError("VC runtime input hash mismatch")
            shutil.copy2(source, runtime / source.name)
        destination = output / "licenses/microsoft-vc"
        destination.mkdir(parents=True)
        shutil.copy2(license_file, destination / "LICENSE.txt")
        (destination / "redistribution.json").write_text(json.dumps(provenance, indent=2), encoding="utf-8")
        vc_component = dict(component="Microsoft VC runtime", version=provenance["version"], distributed=True,
            license="Microsoft Software License Terms; OWNER REVIEW REQUIRED", source_url=provenance["source_url"],
            review_status="OWNER REVIEW REQUIRED", review="Verified official bytes/signatures do not establish owner redistribution entitlement",
            notice_files=["microsoft-vc/LICENSE.txt", "microsoft-vc/redistribution.json"])
    helper = output / "ocr/helper"
    helper.mkdir(parents=True)
    shutil.copy2(ROOT / "helpers/ocr/paddle_helper.py", helper / "paddle_helper.py")
    for entry in catalog["files"]:
        destination = output / "ocr/models" / entry["path"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(models / entry["path"], destination)
        if digest(destination) != entry["sha256"]:
            raise ValueError("Model input changed while copying")
    matrix = collect_licenses(runtime, python_base, qt, mingw, output / "licenses")
    if vc_component:
        matrix.append(vc_component)
    if args.license_root:
        notices = json.loads((ROOT / "scripts/runtime-notices.json").read_text(encoding="utf-8"))
        for item in notices["files"]:
            source = relative_file(args.license_root.resolve(), item["path"])
            if digest(source) != item["sha256"]:
                raise ValueError("Supplementary notice hash mismatch")
            destination = output / "licenses/supplementary" / item["path"]
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        matrix = [i for i in matrix if i["component"] != "PP-OCRv6 small det/rec"]
        for kind in ("det", "rec"):
            name = "PP-OCRv6_small_" + kind
            matrix.append(dict(component=name, version="Phase 6.1A verified assets", license="Apache-2.0",
                distributed=True, source_url="https://huggingface.co/PaddlePaddle/" + name,
                notice_files=["supplementary/models/" + name + "_MODEL_CARD.md", "supplementary/models/Apache-2.0.txt"],
                review_status="REVIEW REQUIRED", review="Official card declaration and weight identity verified; preserve notices; owner release review outstanding"))
        (output / "licenses/supplementary/provenance.json").write_text(json.dumps(notices, indent=2), encoding="utf-8")
    (output / "licenses/component-matrix.json").write_text(json.dumps(matrix, indent=2), encoding="utf-8")
    (output / "README.txt").write_text(
        "Translator Windows x64 draft portable package. Clean-machine acceptance pending.\n"
        "Run Translator.exe; select PP-OCRv6 Small, Region, Start. OCR uses bundled local models.\n"
        "Tesseract is not bundled: its third-party binary license/dependency review is pending.\n"
        "Settings remain per-user; configure provider credentials on each machine. Keys are not included.\n"
        "Unsigned binaries may trigger SmartScreen. Do not disable security controls.\n"
        "Redistribution/license review pending; this is a local technical acceptance artifact.\n", encoding="ascii")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip())
    manifest = dict(schema=1, app_version="0.2.0", commit=commit, source_dirty=dirty, packaging_mode="portable-python",
                    versions=dict(VERSIONS, qt="6.11.2", python="3.12.14", mingw="13.1"), mkldnn=False,
                    model_names=["PP-OCRv6_small_det", "PP-OCRv6_small_rec"], tesseract="not-bundled",
                    clean_machine_acceptance="pending", license_review="pending", files=[])
    manifest["physically_offline_acceptance"] = "pending"
    for path in sorted(p for p in output.rglob("*") if p.is_file()):
        manifest["files"].append(dict(path=path.relative_to(output).as_posix(), bytes=path.stat().st_size, sha256=digest(path)))
    (output / "runtime-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    validate(output)
    audit = dependency_audit(output)
    report = ROOT / ".cache/packaging-validation/dependencies.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(audit, indent=2), encoding="utf-8")
    print(f"Draft package: {len(manifest['files'])} files; {sum(f['bytes'] for f in manifest['files']) / 1048576:.2f} MiB")
    if audit["missing_count"]:
        raise ValueError(f"Native dependency candidates missing: {audit['missing_count']}; see local audit")
    if args.smoke:
        import sys
        run([sys.executable, ROOT / "scripts/validate_windows_package.py", output, "--smoke"], env)


if __name__ == "__main__":
    main()
