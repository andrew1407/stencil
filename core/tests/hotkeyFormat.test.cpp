#include "hotkeyFormat.hpp"
#include "doctest.h"

using namespace stencil::core::hotkeyFormat;

TEST_CASE("toNative renders Apple symbols and ordering on macOS") {
  CHECK(toNative("Ctrl+C", true) == "⌘C");
  // Apple order is ⌃⌥⇧⌘, so Ctrl+Shift collapses to ⇧⌘ before the key.
  CHECK(toNative("Ctrl+Shift+Z", true) == "⇧⌘Z");
  CHECK(toNative("Alt+A", true) == "⌥A");
  CHECK(toNative("Alt+Up", true) == "⌥↑");
  CHECK(toNative("Alt+0", true) == "⌥0");
}

TEST_CASE("toNative orders all four modifiers as Control,Option,Shift,Command") {
  CHECK(toNative("Ctrl+Alt+Shift+Meta+K", true) == "⌃⌥⇧⌘K");
  CHECK(toNative("Alt+Down", true) == "⌥↓");
  CHECK(toNative("Alt+Left", true) == "⌥←");
  CHECK(toNative("Alt+Right", true) == "⌥→");
}

TEST_CASE("toNative leaves the portable string unchanged on non-macOS") {
  CHECK(toNative("Ctrl+Shift+Z", false) == "Ctrl+Shift+Z");
  CHECK(toNative("Ctrl+C", false) == "Ctrl+C");
  CHECK(toNative("Alt+Up", false) == "Alt+Up");
  CHECK(toNative("F1", false) == "F1");
}

TEST_CASE("toNative is robust to empty, lone, and unknown tokens") {
  CHECK(toNative("", true) == "");
  CHECK(toNative("", false) == "");
  CHECK(toNative("F1", true) == "F1");           // lone key, no modifiers
  CHECK(toNative("Bogus", true) == "Bogus");     // unknown token passes through
  CHECK(toNative("Ctrl+Bogus", true) == "⌘Bogus");
}

TEST_CASE("isMacBuild reflects the current build platform") {
#ifdef __APPLE__
  CHECK(isMacBuild() == true);
#else
  CHECK(isMacBuild() == false);
#endif
}
