#!/bin/sh
# Downloads the pictures straight from Epic0522's repository and packs them into minimap.pack.
# The pictures are Epic0522's (and the map authors'); they are not part of this repository.
# usage: sh tools/get_minimap_pack.sh [out/minimap.pack]
set -e
OUT=${1:-minimap.pack}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
git clone --depth 1 --filter=blob:none --sparse https://github.com/Epic0522/Super-Mario-64-3ds-port---Ultimate "$TMP/3ds"
git -C "$TMP/3ds" sparse-checkout set src/minimap
python3 ${PYFLAGS--I} "$HERE/make_minimap_pack.py" "$TMP/3ds" "$OUT"
