#pragma once
// Dragging the header mark (browser twin js/ui/drag/logoDrag.js): over the canvas the picture
// previews a clean view — no filter, lines, points or compare, painted only, nothing set — and a
// drop there commits it through the toolbar's own appliers; anywhere else nothing changes.
#include <QPixmap>
#include <functional>

class QToolButton;
class QWidget;

namespace stencil::gui {

  class LogoStage;

  struct LogoDragHooks {
    std::function<QPixmap()> mark;          // the ghost's art: the button paints no mark itself
    std::function<bool()> free;             // no modal, no popover, no click a closed one still owns
    std::function<QWidget*()> canvas;       // the canvas region, which glows as the drop target
    std::function<bool()> hasImage;         // no picture, no clean view to preview or set
    std::function<void(bool on)> preview;   // the canvas's view-only clean view
    std::function<void()> commit;           // the clean view, through the ordinary appliers
  };

  // A modified press is the accent menu's and one whose hold opened a show is the show's, so
  // neither drags; a drag reads to the hold as a move away.
  void installLogoDrag(QToolButton* logo, const LogoStage* stage, LogoDragHooks hooks);

}  // namespace stencil::gui
