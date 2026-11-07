#pragma once
// One row that forms out of, and falls into, a cloud of its own, sliding its own height open
// and shut. Desktop twin of browser/js/ui/motion/dust/row.js.
#include "../../../support/motion/DisintegrateOverlay.hpp"
#include "chipClocks.hpp"

class QPixmap;
class QVariantAnimation;
class QWidget;

namespace stencil::gui {

  inline constexpr int CHIP_CELL_PX = 2;

  // A cloud over `w`, raised in `host`'s coordinates so it survives the row moving under it.
  DisintegrateOverlay* chipCloud(QWidget* host, QWidget* w, const QPixmap& shot,
                                 DisintegrateOverlay::Sweep sweep);

  // Sliding / Transition fly no cloud: `shot` slides out of `w`'s left edge or grows from 0.78 over `host`,
  // veiling `w` until it lands; null in any other mode. Browser twin: row.js modeFlight.
  QWidget* modeFlight(QWidget* host, QWidget* w, const QPixmap& shot, bool show, int ms);

  // A picture's flight between `origin` (the control that loaded it) and `w` over `host`: poured or grown,
  // veiling `w` until it lands. Its length in ms, 0 in any other mode. Browser twin: openImage/previewGenie.js.
  int mediaFlight(QWidget* host, QWidget* w, const QPixmap& shot, bool show, QWidget* origin);

  // `row` eased between 0 and its own hint, on `anim` (created on first use, owned by `owner`).
  void slideRowHeight(QObject* owner, QWidget* row, QVariantAnimation*& anim, bool show);

}  // namespace stencil::gui
