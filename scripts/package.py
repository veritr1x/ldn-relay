"""Validate the built NRO and package only redistributable download files."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess

from privacy_check import findings, public_files

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=root / 'dist')
args = parser.parse_args()
subprocess.run(['python3', str(root / 'scripts/privacy_check.py')], check=True)
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
icon = data[nro_size + icon_offset:nro_size + icon_offset + icon_size]
if icon != (root / "assets/icon.jpg").read_bytes():
    raise SystemExit("NRO icon does not match the approved project artwork")
nacp_offset, nacp_size = assets[1]
if nacp_size != 0x4000:
    raise SystemExit("Invalid NACP size")
nacp = data[nro_size + nacp_offset:nro_size + nacp_offset + nacp_size]
title = nacp[:0x200].split(b"\0", 1)[0].decode("utf-8")
author = nacp[0x200:0x300].split(b"\0", 1)[0].decode("utf-8")
embedded_version = nacp[0x3060:0x3070].split(b"\0", 1)[0].decode("utf-8")
if title != "LDN Relay" or author != "veritrix" or embedded_version != version:
    raise SystemExit(f"Unexpected title/author/version: {title!r} / {author!r} / {embedded_version!r}")

for language in range(16):
    entry=nacp[language*0x300:(language+1)*0x300]
    if not any(entry):
        continue  # nacptool leaves unsupported language slots empty
    if entry[:0x200].split(b"\0",1)[0] != b"LDN Relay" or entry[0x200:].split(b"\0",1)[0] != b"veritrix":
        raise SystemExit("Unexpected localized title/author")
if findings(nro.name, data):
    raise SystemExit("NRO contains a private identifier/path")
ref = os.environ.get("GITHUB_REF", "")
if ref.startswith("refs/tags/") and ref != "refs/tags/v" + version:
    raise SystemExit("Release tag does not match embedded version")

revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True))
if os.environ.get("CI") and dirty:
    raise SystemExit("Refusing to package an uncommitted CI checkout")
toolchain = re.search(r"devkitpro/devkita64@sha256:[0-9a-f]{64}", (root / "build.sh").read_text()).group()
digest = hashlib.sha256(data).hexdigest()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
# Never mix an older download with the current one.
if any(out.iterdir()):
    raise SystemExit("Output directory must be empty; existing packages are never overwritten")
shutil.copy2(nro, out / nro.name)
for name in ("README.md", "LICENSE", "THIRD_PARTY.md", "PRODUCTION_READINESS.md", "PRIVACY.md", "PAIRING.md", "TODO.md", "INSTALL.md", "COMPATIBILITY.md", "TROUBLESHOOTING.md", "SECURITY.md", "CHANGELOG.md", "CONTRIBUTING.md"):
    shutil.copy2(root / name, out / name)
shutil.copytree(root / "licenses", out / "licenses")
shutil.copytree(root / "assets", out / "assets")
(out / "relay").mkdir()
shutil.copy2(root / "relay/PROTOCOL.md", out / "relay/PROTOCOL.md")
(out / "SHA256SUMS").write_text(f"{digest}  {nro.name}\n")
record = {
    "version": version, "title": title, "author": author, "file": nro.name,
    "size": len(data), "sha256": digest, "icon_sha256": hashlib.sha256(icon).hexdigest(), "source_commit": revision,
    "source_dirty": dirty, "toolchain_image": toolchain,
    "release_status": "preview; hardware qualification incomplete",
    "source_url": f"https://github.com/veritr1x/ldn-relay/tree/{revision}",
    "validation": "NRO header, segment bounds, asset bounds, approved JPEG icon, NACP titles, author and version; privacy check",
    "hardware_validation": "CI performs build and codec checks only; see PRODUCTION_READINESS.md for hardware results",
}
# Record the actual local source, including uncommitted work, without paths to
# the developer machine. A commit SHA alone is insufficient for dirty builds.
manifest={name:hashlib.sha256((root/name).read_bytes()).hexdigest()
          for name in public_files() if (root/name).is_file()}
encoded=json.dumps(manifest,sort_keys=True,indent=2)+"\n"
(out / "source-files.json").write_text(encoded)
record["source_manifest_sha256"]=hashlib.sha256(encoded.encode()).hexdigest()
(out / "build-info.json").write_text(json.dumps(record, indent=2) + "\n")
print(json.dumps(record, indent=2))

checksums=[]
for path in sorted(out.rglob('*')):
    if path.is_file() and path.name!='SHA256SUMS':
        checksums.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(out)}")
(out/'SHA256SUMS').write_text('\n'.join(checksums)+'\n')
