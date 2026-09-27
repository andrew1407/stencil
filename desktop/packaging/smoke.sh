#!/usr/bin/env bash
# Starts a packaged desktop app from its own files (never the runner's Qt) and fails on a
# library or plugin the deploy left out. Linux .tar.gz and macOS .dmg; smoke.ps1 is Windows.
#   desktop/packaging/smoke.sh <package>
set -euo pipefail
pkg=$1
work=$(mktemp -d)
unset LD_LIBRARY_PATH QT_PLUGIN_PATH QML2_IMPORT_PATH DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH

need() { [[ -f "$1" ]] || { echo "smoke: the package carries no ${1#"$work"/}" >&2; exit 1; }; }

case "$pkg" in
  *.tar.gz)
    tar -xzf "$pkg" -C "$work"
    root=$(echo "$work"/stencil-*)
    for p in platforms/libqxcb imageformats/libqjpeg imageformats/libqwebp multimedia/libffmpegmediaplugin; do
      need "$root/plugins/$p.so"
    done
    run=(xvfb-run -a "$root/bin/stencil") ;;
  *.dmg)
    hdiutil attach -nobrowse -readonly -mountpoint "$work/mnt" "$pkg" >/dev/null
    trap 'hdiutil detach "$work/mnt" >/dev/null' EXIT
    root="$work/mnt/stencil.app/Contents"
    for p in platforms/libqcocoa imageformats/libqjpeg imageformats/libqwebp multimedia/libffmpegmediaplugin; do
      need "$root/PlugIns/$p.dylib"
    done
    run=("$root/MacOS/stencil") ;;
  *) echo "smoke: unknown package $pkg" >&2; exit 2 ;;
esac

# QApplication loads the platform plugin before --help is parsed, so usage on stdout proves it.
out=$("${run[@]}" --help)
grep -q '^Usage:' <<<"$out" || { echo "smoke: no usage from $pkg" >&2; exit 1; }
echo "smoke: $(basename "$pkg") starts from its own files"
