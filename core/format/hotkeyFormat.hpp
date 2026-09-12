#pragma once
#include <string>

// Display form of a portable key sequence (the Windows/Linux notation of
// browser/js/config/hotkeysConfig.json, e.g. "Ctrl+Shift+Z"). On macOS that is Apple
// symbols in Apple order with no '+' ("⇧⌘Z"); elsewhere the string is returned unchanged.
namespace stencil::core::hotkeyFormat {

  // isMac is a parameter so the mapping is testable without Qt; unknown tokens and
  // lone keys ("F1") pass through.
  std::string toNative(const std::string& portable, bool isMac);

  bool isMacBuild();

}
