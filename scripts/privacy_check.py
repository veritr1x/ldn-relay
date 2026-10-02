#!/usr/bin/env python3
"""Check publishable working-tree files, excluding ignored local evidence/builds.
Third-party licence notices retain their legally required attributions.
This is a small privacy guard, not a general secret scanner.
"""
from pathlib import Path
import argparse
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PATTERNS = (
    re.compile(rb'/' rb'Users/[^/\s]+/'),
    re.compile(rb'[A-Za-z]:\\Users\\[^\\\s]+\\'),
    re.compile(rb'\b[0-9A-Fa-f]{8}-[0-9A-Fa-f]{16}\b'),  # Apple hardware UDID
    re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
)
PRIVATE_SUFFIXES = {'.mobileprovision', '.provisionprofile', '.p12', '.p8', '.key', '.pem', '.sav', '.gba', '.jsonl', '.log'}

def findings(name, data, deny=()):
    issues = []
    if Path(name).suffix.lower() in PRIVATE_SUFFIXES:
        issues.append('private artifact type')
    if any(p.search(data) for p in PATTERNS):
        issues.append('local path, device identifier or private key')
    if any(token.lower().encode() in data.lower() for token in deny):
        issues.append('denied personal identifier')
    return issues

def public_files():
    raw = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT)
    return sorted({p.decode() for p in raw.split(b'\0') if p})

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deny-token', action='append', default=[])
    args = parser.parse_args()
    failed = []
    for name in public_files():
        path = ROOT / name
        if not path.is_file() or name.startswith('licenses/'):
            continue
        for issue in findings(name, path.read_bytes(), args.deny_token):
            failed.append(f'{name}: {issue}')
    if failed:
        raise SystemExit('\n'.join(failed))
    print('PASS publishable source privacy check (ignored local artifacts excluded)')

if __name__ == '__main__':
    main()
