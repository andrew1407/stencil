#pragma once
// Dragging the header mark (browser twin js/ui/drag/logoDrag.js): over the bare canvas a painted
// clean view, committed by a drop there through the toolbar's appliers; a line dropped on takes the
// toolbar's style, a control with a default goes back to it (support/control/dblReset.hpp).
#include <QPixmap>
#include <QPoint>
#include <functional>

class QToolButton;
class QWidget;

namespace stencil::gui {

  class LogoStage;

  // The line a drop restyles (-1: none) and what glows for it.
  struct LogoLineAim {
    int idx = -1;
    QWidget* glow = nullptr;
  };

  struct LogoDragHooks {
    std::function<QPixmap()> mark;          // the ghost's art: the button paints no mark itself
    std::function<bool()> free;             // no modal, no popover, no click a closed one still owns
    std::function<QWidget*()> canvas;       // the canvas region, which glows as the drop target
    std::function<bool()> hasImage;         // no picture, no clean view to preview or set
    std::function<void(bool on)> preview;   // the canvas's view-only clean view
    std::function<void()> commit;           // the clean view, through the ordinary appliers
    std::function<LogoLineAim(QWidget* at, const QPoint& global)> lineAt;
    std::function<void(int idx)> lineHover;   // the canvas hover glow on a line; -1 clears it
    std::function<void(int idx)> styleLine;   // the toolbar's style onto line idx, one undo step
  };

  // A modified press is the accent menu's and one whose hold opened a show is the show's, so
  // neither drags; a drag reads to the hold as a move away.
  void installLogoDrag(QToolButton* logo, const LogoStage* stage, LogoDragHooks hooks);

}  // namespace stencil::gui
