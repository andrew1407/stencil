#!/usr/bin/env bash
# Build a macOS .icns from an SVG, rasterising each iconset size straight from
# the vector (sharp at every resolution) and packing them with iconutil.
#
# Usage: make-icns.sh <input.svg> <output.icns>
#
# macOS-only: relies on `sips` (SVG -> PNG) and `iconutil` (.iconset -> .icns),
# both shipped with the base OS. Called at configure time by desktop/CMakeLists.txt
# so the app icon stays derived from browser/favicon.svg (single source of truth,
# same SVG the Linux/browser builds use) rather than a committed binary.
set -euo pipefail

src=${1:?usage: make-icns.sh <input.svg> <output.icns>}
out=${2:?usage: make-icns.sh <input.svg> <output.icns>}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
set="$work/stencil.iconset"
mkdir -p "$set"

# name -> pixel size; @2x retina variants are the same pixels as the next size up.
emit() { # <pixels> <iconset-filename>
  sips -s format png -z "$1" "$1" "$src" --out "$set/$2" >/dev/null
}
emit 16   icon_16x16.png
emit 32   icon_16x16@2x.png
emit 32   icon_32x32.png
emit 64   icon_32x32@2x.png
emit 128  icon_128x128.png
emit 256  icon_128x128@2x.png
emit 256  icon_256x256.png
emit 512  icon_256x256@2x.png
emit 512  icon_512x512.png
emit 1024 icon_512x512@2x.png

iconutil -c icns "$set" -o "$out"
echo "wrote $out"
