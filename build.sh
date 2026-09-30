#!/bin/sh
set -eu
cd "$(dirname "$0")"
exec docker run --rm --network none \
  --user "$(id -u):$(id -g)" \
  --mount "type=bind,source=$(pwd),target=/project" \
  --workdir /project \
  devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282 \
  make -j2 "$@"
