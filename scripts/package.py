"""Validate the built NRO and package only redistributable download files."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess

root = Path(__file__).resolve().parents[1]
header = (root / "relay/common/relay_protocol.h").read_text()
match = re.search(r'^#define RELAY_VERSION "(\d+\.\d+\.\d+)"$', header, re.M)
if not match:
    raise SystemExit("Cannot determine relay version")
version = match.group(1)
nro = root / "build" / f"ldn-relay-v{version}.nro"
data = nro.read_bytes()
if len(data) < 0x80 or data[0x10:0x14] != b"NRO0":
    raise SystemExit("Invalid NRO header")
nro_size = struct.unpack_from("<I", data, 0x18)[0]
if nro_size < 0x80 or nro_size + 0x38 > len(data):
    raise SystemExit("Invalid NRO size or missing assets")
for offset in (0x20, 0x28, 0x30):
    start, size = struct.unpack_from("<II", data, offset)
    if start + size > nro_size:
        raise SystemExit("NRO segment outside executable")
if data[nro_size:nro_size + 4] != b"ASET":
    raise SystemExit("Missing NRO asset header")
if struct.unpack_from("<I", data, nro_size + 4)[0] != 0:
    raise SystemExit("Unsupported NRO asset version")
assets = []
for offset in (8, 24, 40):
    start, size = struct.unpack_from("<QQ", data, nro_size + offset)
    if size and (start < 0x38 or nro_size + start + size > len(data)):
        raise SystemExit("Asset outside NRO file")
    assets.append((start, size))
icon_offset, icon_size = assets[0]
if icon_size == 0 or data[nro_size + icon_offset:nro_size + icon_offset + 2] != b"\xff\xd8":
    raise SystemExit("Missing JPEG icon")
nacp_offset, nacp_size = assets[1]
if nacp_size != 0x4000:
    raise SystemExit("Invalid NACP size")
nacp = data[nro_size + nacp_offset:nro_size + nacp_offset + nacp_size]
title = nacp[:0x200].split(b"\0", 1)[0].decode("utf-8")
embedded_version = nacp[0x3060:0x3070].split(b"\0", 1)[0].decode("utf-8")
if title != "LDN Relay" or embedded_version != version:
    raise SystemExit(f"Unexpected title/version: {title!r} / {embedded_version!r}")

revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True))
if os.environ.get("CI") and dirty:
    raise SystemExit("Refusing to package an uncommitted CI checkout")
toolchain = re.search(r"devkitpro/devkita64@sha256:[0-9a-f]{64}", (root / "build.sh").read_text()).group()
digest = hashlib.sha256(data).hexdigest()
out = root / "dist"
out.mkdir(exist_ok=True)
# Never mix an older download with the current one.
if any(out.iterdir()):
    raise SystemExit("dist/ must be empty; move the previous package before rebuilding")
shutil.copy2(nro, out / nro.name)
for name in ("README.md", "LICENSE", "THIRD_PARTY.md"):
    shutil.copy2(root / name, out / name)
shutil.copytree(root / "licenses", out / "licenses")
shutil.copy2(root / "relay/PROTOCOL.md", out / "PROTOCOL.md")
(out / "SHA256SUMS").write_text(f"{digest}  {nro.name}\n")
record = {
    "version": version, "title": title, "file": nro.name,
    "size": len(data), "sha256": digest, "source_commit": revision,
    "source_dirty": dirty, "toolchain_image": toolchain,
    "source_url": f"https://github.com/veritr1x/ldn-relay/tree/{revision}",
    "validation": "NRO header, segment bounds, asset bounds, JPEG icon, NACP title and version",
    "hardware_validation": "CI performs build and codec checks only; see README for prior hardware results",
}
(out / "build-info.json").write_text(json.dumps(record, indent=2) + "\n")
print(json.dumps(record, indent=2))
