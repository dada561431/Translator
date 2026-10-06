"""Extract a pinned Microsoft Burn bundle as data, never execute its MSI payloads."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import struct
import subprocess
import xml.etree.ElementTree as ET
import zipfile
from portable_runtime import digest

SOURCE = "https://aka.ms/vc14/vc_redist.x64.exe"
VERSION = "14.51.36247.0"
INSTALLER_SHA256 = "843068991daaa1f73ad9f6239bce4d0f6a07a51f18c37ea2a867e9beca71295c"
LICENSE_SOURCE = "https://visualstudio.microsoft.com/wp-content/uploads/2025/10/Visual-C-V14-License-Redistributable_and_Runtime_ENU.docx"


def cabinet_ranges(header, total):
    # Burn v2 on-disk section layout, not a scan for incidental CAB magic bytes.
    # Format reference: wixtoolset/wix3 src/burn/engine/section.cpp.
    if len(header) < 56:
        raise ValueError("Truncated Burn header")
    magic, version = struct.unpack_from("<II", header)
    stub, _, signature, signature_size, fmt, count = struct.unpack_from("<6I", header, 24)
    if (magic, version, fmt, count) != (0x00F14300, 2, 1, 2):
        raise ValueError("Unsupported Burn container layout")
    ux, attached = struct.unpack_from("<II", header, 48)
    offset = signature + signature_size if signature else stub + ux
    ranges = [(stub, ux), (offset, attached)]
    if any(size <= 0 or start < 0 or start + size > total for start, size in ranges):
        raise ValueError("Burn container out of bounds")
    if stub < 56 or offset < stub + ux:
        raise ValueError("Overlapping Burn containers")
    return ranges


def signature(path, powershell="powershell.exe"):
    command = "$ErrorActionPreference='Stop'; $s=Get-AuthenticodeSignature -LiteralPath $args[0]; " \
              "@{status=[string]$s.Status;signer=$s.SignerCertificate.Subject}|ConvertTo-Json -Compress"
    # Pass file via an environment variable to avoid PowerShell source interpolation.
    import os
    env = {k: v for k, v in os.environ.items() if k.upper() != "PSMODULEPATH"}
    env["TRANSLATOR_SIGNATURE_FILE"] = str(path.resolve())
    command = command.replace("$args[0]", "$env:TRANSLATOR_SIGNATURE_FILE")
    record = json.loads(subprocess.check_output([str(powershell), "-NoProfile", "-Command", command], env=env, text=True))
    if record["status"] != "Valid" or "O=Microsoft Corporation," not in record["signer"]:
        raise ValueError("Expected a valid Microsoft Authenticode signature")
    return record


def extract(sevenzip, cabinet, destination):
    subprocess.run([str(sevenzip), "x", str(cabinet), "-o" + str(destination), "-y"], check=True,
                   stdout=subprocess.DEVNULL)


def main():
    import pefile
    parser = argparse.ArgumentParser()
    parser.add_argument("--redist", type=Path, required=True)
    parser.add_argument("--sevenzip", type=Path, required=True)
    parser.add_argument("--license-docx", type=Path, required=True)
    parser.add_argument("--audit", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--powershell", default="powershell.exe", help="Build-only PowerShell with Authenticode support")
    args = parser.parse_args()
    if digest(args.redist) != INSTALLER_SHA256:
        raise ValueError("Microsoft bundle hash does not match the reviewed input")
    signed = signature(args.redist, args.powershell)
    output = args.output.resolve()
    if output.exists():
        raise ValueError("Use a new task-owned output; this tool never deletes an existing root")
    output.mkdir(parents=True)
    work = output / "extraction"
    work.mkdir()
    data = args.redist.read_bytes()
    pe = pefile.PE(data=data, fast_load=True)
    header = next(s.get_data() for s in pe.sections if s.Name.rstrip(b"\0") == b".wixburn")
    pe.close()
    directories = []
    for index, (start, size) in enumerate(cabinet_ranges(header, len(data))):
        cabinet = work / f"container-{index}.cab"
        cabinet.write_bytes(data[start:start + size])
        directory = work / str(index)
        extract(args.sevenzip.resolve(), cabinet, directory)
        directories.append(directory)
    manifest = ET.parse(directories[0] / "0")
    ns = {"b": "http://schemas.microsoft.com/wix/2008/Burn"}
    payload_cabs = []
    for item in manifest.findall("b:Payload", ns):
        payload = directories[1] / item.attrib["SourcePath"]
        if (payload.stat().st_size != int(item.attrib["FileSize"])
                or hashlib.sha1(payload.read_bytes()).hexdigest().upper() != item.attrib["Hash"]):
            raise ValueError("Burn payload integrity mismatch")
        if "amd64" in item.attrib["FilePath"] and item.attrib["FilePath"].endswith(".cab"):
            target = work / payload.name
            extract(args.sevenzip.resolve(), payload, target)
            payload_cabs.append(target)
    audit = json.loads(args.audit.read_text())
    needed = {i["dependency"] for i in audit["imports"]
              if re.fullmatch(r"(?:msvcp|vcruntime|concrt|vcomp)\d+(?:_[a-z0-9]+)*\.dll", i["dependency"])}
    # Wheel-vendored hash-renamed DLLs already belong to their wheel; do not replace them.
    found = {}
    for directory in payload_cabs:
        for path in directory.iterdir():
            try:
                dll = pefile.PE(str(path))
            except pefile.PEFormatError:
                continue
            if dll.FILE_HEADER.Machine != 0x8664:
                dll.close()
                continue
            for info in getattr(dll, "FileInfo", []):
                for entry in info:
                    for table in getattr(entry, "StringTable", []):
                        original = table.entries.get(b"OriginalFilename", b"").decode().lower()
                        if original in needed:
                            version = dll.VS_FIXEDFILEINFO[0]
                            actual = ".".join(map(str, (version.FileVersionMS >> 16, version.FileVersionMS & 65535,
                                                      version.FileVersionLS >> 16, version.FileVersionLS & 65535)))
                            if actual != VERSION:
                                raise ValueError("Mixed VC runtime versions")
                            target = output / original
                            shutil.copy2(path, target)
                            found[original] = dict(path=original, sha256=digest(target), bytes=target.stat().st_size,
                                                   version=actual, signature=signature(target, args.powershell))
            dll.close()
    if set(found) != needed:
        raise ValueError("Official CABs did not provide all audited CRT imports: " + str(needed - set(found)))
    with zipfile.ZipFile(args.license_docx) as archive:
        document = ET.fromstring(archive.read("word/document.xml"))
    word = {"w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main"}
    paragraphs = ["".join(t.text or "" for t in p.findall(".//w:t", word)) for p in document.findall(".//w:p", word)]
    if not any("MICROSOFT" in p.upper() and "LICENSE" in p.upper() for p in paragraphs):
        raise ValueError("License input does not contain Microsoft license terms")
    (output / "LICENSE.txt").write_text("\n".join(paragraphs) + "\n", encoding="utf-8")
    record = dict(version=VERSION, source_url=SOURCE, installer_sha256=INSTALLER_SHA256,
                  installer_signature=signed, license_source_url=LICENSE_SOURCE,
                  license_input_sha256=digest(args.license_docx), license_file="LICENSE.txt",
                  license_sha256=digest(output / "LICENSE.txt"),
                  review_status="OWNER REVIEW REQUIRED", owner_redistribution_confirmed=False,
                  extraction="Burn v2 CABs and verified payloads; no installation; x64 actual PE import cohort",
                  files=sorted(found.values(), key=lambda i: i["path"]))
    (output / "redistribution.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
    print(json.dumps(record, indent=2))


if __name__ == "__main__":
    main()
