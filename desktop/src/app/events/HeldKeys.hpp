#pragma once
#include <QtGlobal>

namespace stencil::gui {

  // Keys the window is holding down: R, for the selected-line rotate chord; and when ~ last went down.
  struct HeldKeys {
    bool r = false;
    quint64 tildeAtMs = 0;   // event timestamp; 0 = no first press pending
  };

}  // namespace stencil::gui
