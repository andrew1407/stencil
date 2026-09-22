#include "hotkeyFormat.hpp"
#include "text.hpp"
#include <array>
#include <string>
#include <vector>

namespace stencil::core::hotkeyFormat {

  namespace {
    // `order` is the Apple order: Control, Option, Shift, Command; -1 is "not a modifier".
    struct MacModifier {
      int order;
      std::string_view glyph;
    };

    // Ctrl→⌘ for DISPLAY (Qt swaps Ctrl/Meta on macOS for matching, but the table shows
    // the key users press).
    constexpr std::array<Keyed<MacModifier>, 4> MAC_MODIFIERS = {{
        {"Meta", {0, "⌃"}},
        {"Alt", {1, "⌥"}},
        {"Shift", {2, "⇧"}},
        {"Ctrl", {3, "⌘"}},
    }};

    constexpr std::array<Keyed<std::string_view>, 4> MAC_KEYS = {{
        {"Up", "↑"},
        {"Down", "↓"},
        {"Left", "←"},
        {"Right", "→"},
    }};

    std::string macModifier(const std::string& token, int& orderOut) {
      const MacModifier m = lookup(MAC_MODIFIERS, token, MacModifier{-1, ""});
      orderOut = m.order;
      return std::string(m.glyph);
    }

    std::string macKey(const std::string& token) {
      return std::string(lookup(MAC_KEYS, token, std::string_view(token)));
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

    // Anything not recognized as a modifier is the key, so unknown tokens pass through.
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
