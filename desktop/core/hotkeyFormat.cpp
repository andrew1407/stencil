#include "hotkeyFormat.hpp"
#include <string>
#include <vector>

namespace stencil::core::hotkeyFormat {

  namespace {
    // Apple-symbol modifier mapping. For DISPLAY of the portable config strings
    // the user-facing intent is Ctrl→⌘ (Qt swaps Ctrl/Meta on macOS for
    // matching, but the table shows the command users actually press).
    // Apple ordering is Control, Option, Shift, Command, then the key.
    std::string macModifier(const std::string& token, int& orderOut) {
      if (token == "Meta")  { orderOut = 0; return "⌃"; }  // ⌃ Control
      if (token == "Alt")   { orderOut = 1; return "⌥"; }  // ⌥ Option
      if (token == "Shift") { orderOut = 2; return "⇧"; }  // ⇧ Shift
      if (token == "Ctrl")  { orderOut = 3; return "⌘"; }  // ⌘ Command
      orderOut = -1;
      return {};
    }

    // Arrow key names → Apple arrow glyphs (also the natural display elsewhere).
    std::string macKey(const std::string& token) {
      if (token == "Up")    return "↑";  // ↑
      if (token == "Down")  return "↓";  // ↓
      if (token == "Left")  return "←";  // ←
      if (token == "Right") return "→";  // →
      return token;
    }

    std::vector<std::string> split(const std::string& s, char sep) {
      std::vector<std::string> out;
      std::string cur;
      for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
      }
      out.push_back(cur);
      return out;
    }
  }

  std::string toNative(const std::string& portable, bool isMac) {
    if (!isMac || portable.empty()) return portable;

    const std::vector<std::string> tokens = split(portable, '+');

    // Collect modifier glyphs keyed by Apple order; the final non-modifier
    // token(s) form the key. Anything not recognized as a modifier is treated
    // as the key, so lone keys ("F1") and unknown tokens pass through.
    std::string mods[4];      // indexed by Apple order 0..3
    bool hasMod[4] = {false, false, false, false};
    std::string keyPart;

    for (const auto& tok : tokens) {
      int order = -1;
      const std::string glyph = macModifier(tok, order);
      if (order >= 0) {
        mods[order] = glyph;
        hasMod[order] = true;
      } else {
        // Non-modifier token: part of the key. Joining with the original '+'
        // would be unusual; in practice there is a single trailing key.
        keyPart += macKey(tok);
      }
    }

    std::string out;
    for (int i = 0; i < 4; ++i)
      if (hasMod[i]) out += mods[i];
    out += keyPart;
    return out;
  }

  bool isMacBuild() {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
  }

}
