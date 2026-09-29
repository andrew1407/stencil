#!/usr/bin/env bash
# Starts a packaged desktop app from its own files (never the runner's Qt) and fails on a
# library or plugin the deploy left out. Linux .tar.xz and macOS .dmg; smoke.ps1 is Windows.
#   desktop/packaging/smoke.sh <package>
set -euo pipefail
pkg=$1
work=$(mktemp -d)
unset LD_LIBRARY_PATH QT_PLUGIN_PATH QML2_IMPORT_PATH DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH

need() { [[ -f "$1" ]] || { echo "smoke: the package carries no ${1#"$work"/}" >&2; exit 1; }; }

case "$pkg" in
  *.tar.xz)
    tar -xJf "$pkg" -C "$work"
    root=$(echo "$work"/stencil-*)
    for p in platforms/libqxcb imageformats/libqjpeg imageformats/libqwebp multimedia/libffmpegmediaplugin; do
      need "$root/plugins/$p.so"
    done
    # The whole ICU 73 data is 32 MB and the trimmed one 16 MB (packaging/trimicu.cmake).
    for icu in "$root"/lib/libicudata.so.*.*; do
      [[ ! -f "$icu" ]] || (( $(stat -c %s "$icu") < 20000000 )) ||
        { echo "smoke: the bundled ICU still carries its whole data" >&2; exit 1; }
    done
    [[ ! -e "$root/plugins/egldeviceintegrations" ]] ||
      { echo "smoke: the package carries eglfs integrations no shipped platform loads" >&2; exit 1; }
    run=(xvfb-run -a "$root/bin/stencil") ;;
  *.dmg)
    hdiutil attach -nobrowse -readonly -mountpoint "$work/mnt" "$pkg" >/dev/null
    # The just-exited app or Spotlight can hold the image a moment: retry, then force.
    detach() {
      for _ in 1 2 3 4 5; do hdiutil detach "$work/mnt" >/dev/null 2>&1 && return; sleep 2; done
      hdiutil detach -force "$work/mnt" >/dev/null
    }
    trap detach EXIT
    root="$work/mnt/stencil.app/Contents"
    for p in platforms/libqcocoa imageformats/libqjpeg imageformats/libqwebp multimedia/libffmpegmediaplugin; do
      need "$root/PlugIns/$p.dylib"
    done
    codesign --verify --deep --strict "$work/mnt/stencil.app" ||
      { echo "smoke: the bundle's signature does not hold" >&2; exit 1; }
    want=$(lipo -archs "$root/MacOS/stencil")
    other=$(find "$root" -type f -exec lipo -archs {} \; 2>/dev/null | grep -cvx "$want" || true)
    (( other == 0 )) || { echo "smoke: $other binaries carry architectures beyond $want" >&2; exit 1; }
    minos() { otool -l "$1" | awk '/LC_BUILD_VERSION/ {b = 1} b && /minos/ && !m {m = $2} END {print m}'; }
    app_min=$(minos "$root/MacOS/stencil") qt_min=$(minos "$root/Frameworks/QtCore.framework/QtCore")
    [[ $(printf '%s\n' "$app_min" "$qt_min" | sort -V | tail -1) == "$qt_min" ]] ||
      { echo "smoke: the app needs macOS $app_min, but its bundled Qt runs from $qt_min" >&2; exit 1; }
    run=("$root/MacOS/stencil") ;;
  *) echo "smoke: unknown package $pkg" >&2; exit 2 ;;
esac

# QApplication loads the platform plugin before --help is parsed, so usage on stdout proves it.
out=$("${run[@]}" --help)
grep -q '^Usage:' <<<"$out" || { echo "smoke: no usage from $pkg" >&2; exit 1; }
echo "smoke: $(basename "$pkg") starts from its own files"
