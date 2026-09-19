#pragma once
// One row that forms out of, and falls into, a cloud of its own, sliding its own height open
// and shut. Desktop twin of browser/js/ui/motion/dustRow.js.
#include "../support/DisintegrateOverlay.hpp"

class QPixmap;
class QVariantAnimation;
class QWidget;

namespace stencil::gui {

  inline constexpr int CHIP_DUST_MS = 630;
  inline constexpr double CHIP_DUST_DRIFT = 0.15;
  inline constexpr int CHIP_CELL_PX = 2;

  // A cloud over `w`, raised in `host`'s coordinates so it survives the row moving under it.
  DisintegrateOverlay* chipCloud(QWidget* host, QWidget* w, const QPixmap& shot,
                                 DisintegrateOverlay::Sweep sweep);

  // `row` eased between 0 and its own hint, on `anim` (created on first use, owned by `owner`).
  void slideRowHeight(QObject* owner, QWidget* row, QVariantAnimation*& anim, bool show);

}  // namespace stencil::gui
