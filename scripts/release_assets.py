"""Create GitHub download assets from the validated, checksummed CI package."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import re
import shutil
import zipfile


def create_assets(package, output, tag, commit):
    package, output = Path(package), Path(output)
    info = json.loads((package / 'build-info.json').read_text())
    version = info['version']
    if not re.fullmatch(r'\d+\.\d+\.\d+', version) or tag != 'v' + version:
        raise ValueError('Release tag must match the packaged version')
    if info['source_dirty'] or info['source_commit'] != commit:
        raise ValueError('Release requires the exact clean CI commit')
    nro = 'ldn-relay-v' + version + '.nro'
    if info['file'] != nro:
        raise ValueError('Unexpected NRO filename')
    files = {}
    for line in (package / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split('  ', 1)
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name or name in files:
            raise ValueError('Unsafe or duplicate checksum path')
        source = package / name
        if source.is_symlink() or not source.resolve().is_relative_to(package.resolve()):
            raise ValueError('Package must not contain external files')
        data = source.read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError('Package checksum mismatch: ' + name)
        files[name] = data
    actual = {str(p.relative_to(package)) for p in package.rglob('*') if p.is_file()}
    if actual != set(files) | {'SHA256SUMS'}:
        raise ValueError('Every package file must have a checksum')
    if nro not in files or hashlib.sha256(files[nro]).hexdigest() != info['sha256']:
        raise ValueError('NRO does not match build information')
    if not {'README.md', 'LICENSE', 'build-info.json', 'source-files.json'} <= set(files):
        raise ValueError('Package is missing notices or provenance')
    if output.exists() and any(output.iterdir()):
        raise ValueError('Output must be empty; published files are never overwritten')
    output.mkdir(parents=True, exist_ok=True)
    files['SHA256SUMS'] = (package / 'SHA256SUMS').read_bytes()
    archive = output / ('LDN-Relay-' + version + '-Switch.zip')
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name)
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            z.writestr(entry, data)
    for name in (nro, 'build-info.json'):
        shutil.copyfile(package / name, output / name)
    checksums = [hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name
                 for p in sorted(output.iterdir()) if p.is_file()]
    (output / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--commit', required=True)
    args = parser.parse_args()
    create_assets(args.package, args.output, args.tag, args.commit)
