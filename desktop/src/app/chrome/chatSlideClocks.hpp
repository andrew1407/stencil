#pragma once
// The chat dock's slide and its dust flight, one clock each way, ms: common/config/motion.json's
// CHAT_SURFACE_IN_MS / CHAT_SURFACE_OUT_MS through the qrc, the pair the browser's chat panel flies
// on; read once, tests/app/chrome/chatSlideClocks holds them.
#include "../../support/tune/motionTable.hpp"

#include <cmath>

namespace stencil::gui {

  struct ChatSlideClocks {
    int inMs = 630;    // opening: the grains gather while the dock grows
    int outMs = 510;   // leaving, the quicker half; a compact popover waits it out
  };

  inline const ChatSlideClocks& chatSlideClocks() {
    static constexpr ChatSlideClocks DEFAULTS{};   // the struct's own defaults are the fallbacks
    static const ChatSlideClocks c{support::motionTableMs("CHAT_SURFACE_IN_MS", DEFAULTS.inMs),
                                   support::motionTableMs("CHAT_SURFACE_OUT_MS", DEFAULTS.outMs)};
    return c;
  }

  // The docked chat's close, tuned by eye against the browser's (user report): 1.5x quicker, then 1.3x
  // slower again; the table keeps the browser's.
  inline int chatCloseMs() { return static_cast<int>(std::lround(chatSlideClocks().outMs / 1.5 * 1.3)); }

}  // namespace stencil::gui
