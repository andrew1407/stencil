#pragma once
#include <string>

// Pure, GUI-free formatting of a portable key-sequence string (the canonical
// Windows/Linux notation stored in browser/js/config/hotkeysConfig.json, e.g.
// "Ctrl+Shift+Z") into a human-facing display string.
//
// On macOS users expect Apple symbols and Apple ordering (⌃⌥⇧⌘ then the key,
// no '+' separators), so "Ctrl+Shift+Z" -> "⇧⌘Z". On every other platform the
// portable string is the native display, so it is returned unchanged.
//
// STL-only so doctest can exercise the mapping without Qt. The mac-ness is an
// explicit parameter (isMac) to keep the mapping deterministically testable;
// isMacBuild() exposes the current build platform for callers.
namespace stencil::core::hotkeyFormat {

  // Map a portable sequence to its native display form.
  //  - isMac == true : tokens map to Apple symbols (Ctrl→⌘, Alt→⌥, Shift→⇧,
  //    Meta→⌃), arrow names to ↑↓←→, joined in Apple order with no separators.
  //  - isMac == false: the portable string is returned unchanged.
  // Unknown tokens and lone keys (e.g. "F1") pass through untouched.
  std::string toNative(const std::string& portable, bool isMac);

  bool isMacBuild();

}
