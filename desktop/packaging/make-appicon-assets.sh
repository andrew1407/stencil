#!/usr/bin/env bash
# Build a macOS 26 (Tahoe) "Liquid Glass" app icon so the Dock/Finder icon follows
# System Settings → Appearance → "Icon & widget style" (Default / Dark / Tinted /
# Clear). On Tahoe the OS DERIVES all four appearances from ONE layered design — a
# background fill plus foreground layer(s) — packaged as an Icon Composer ".icon"
# and compiled by `actool` into an Assets.car (an IconImageStack / IconGroup).
#
# NOTE: the older asset-catalog route (mac idiom + luminosity "dark"/"tinted"
# appearances) does NOT work — actool silently drops those renditions, so the icon
# stays un-themed. The .icon format is the only one macOS 26 actually tints.
#
# Usage: make-appicon-assets.sh <icon-svg-dir> <out-dir> [actool-path]
#   <icon-svg-dir>  holds appicon-foreground.svg (transparent foreground artwork)
#   <out-dir>       receives Assets.car (+ AppIcon.icns) and appicon-partial.plist
#
# macOS-only and Xcode-only: `actool` ships with Xcode.app, NOT the standalone
# Command Line Tools. CMake calls this at configure time and falls back to the
# plain .icns (make-icns.sh) when actool is absent. Needs `sips` (SVG -> PNG).
set -euo pipefail

svgdir=${1:?usage: make-appicon-assets.sh <icon-svg-dir> <out-dir> [actool]}
outdir=${2:?usage: make-appicon-assets.sh <icon-svg-dir> <out-dir> [actool]}
actool=${3:-}

# Locate a REAL Xcode actool. /usr/bin/actool is only a shim that forwards to the
# active developer dir — and errors out when that's a Command Line Tools instance
# (the common case here), so we must point at Xcode.app's actool directly.
real_actool() {
  local c
  for c in "$1" \
           "$(/usr/bin/xcrun --find actool 2>/dev/null || true)" \
           "${DEVELOPER_DIR:-}/usr/bin/actool" \
           "/Applications/Xcode.app/Contents/Developer/usr/bin/actool"; do
    if [[ -n "$c" && "$c" != "/usr/bin/actool" && -x "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  return 1
}
if ! actool=$(real_actool "$actool"); then
  echo "actool not found (needs full Xcode, not just Command Line Tools)" >&2
  exit 2
fi
export DEVELOPER_DIR="${actool%/usr/bin/actool}"

mkdir -p "$outdir"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
iconbundle="$work/AppIcon.icon"
mkdir -p "$iconbundle/Assets"

# Rasterise the transparent foreground (frame + chart) at full resolution; the OS
# scales it per slot. The dark tile comes from the JSON fill below, NOT the SVG.
sips -s format png -z 1024 1024 "$svgdir/appicon-foreground.svg" \
     --out "$iconbundle/Assets/foreground.png" >/dev/null

# icon.json: a single group with the foreground layer over an auto-gradient fill
# (Stencil's panel colour). macOS composites + themes this; no per-appearance art.
cat > "$iconbundle/icon.json" <<'JSON'
{
  "fill" : { "automatic-gradient" : "extended-srgb:0.16,0.18,0.23,1.0" },
  "groups" : [
    { "layers" : [ { "image-name" : "foreground.png", "name" : "Foreground" } ] }
  ],
  "supported-platforms" : { "circles" : [], "squares" : "shared" }
}
JSON

"$actool" "$iconbundle" \
  --compile "$outdir" \
  --platform macosx \
  --minimum-deployment-target 26.0 \
  --app-icon AppIcon \
  --output-partial-info-plist "$outdir/appicon-partial.plist" \
  --errors --warnings >/dev/null

if [[ ! -f "$outdir/Assets.car" ]]; then
  echo "actool produced no Assets.car" >&2
  exit 3
fi
echo "wrote $outdir/Assets.car"
