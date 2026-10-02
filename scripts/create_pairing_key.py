#!/usr/bin/env python3
"""Create a 256-bit USB provisioning secret. Never prints or overwrites it."""
import argparse
import os
from pathlib import Path
import secrets


def create(path):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as file:
        file.write(secrets.token_bytes(32))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    create(args.output)
    print('Created a private 32-byte pairing key. Transfer over trusted USB only.')
