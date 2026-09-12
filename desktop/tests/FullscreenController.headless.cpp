// Headless check of app/FullscreenController.hpp — the arithmetic fullscreen mode runs
// once the window chrome is out of the way: which edge band counts as a hover reveal
// (and the hysteresis that stops the revealed rows flickering shut under the cursor),
// and the viewport ratio a fullscreen switch hands the canvas so the change of mode
// reads as one zoom. Pure geometry; no widgets, no display.
#include "FullscreenController.hpp"

#include <QCoreApplication>
#include <cmath>

#include "support/check.hpp"

using stencil::gui::FullscreenController;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QSize win(1200, 800);

  // ── Defaults: the mode starts off, and remembers the chrome as shown.
  {
    FullscreenController fs;
    check(!fs.active && !fs.barsShown && !fs.panelShown, "a fresh controller is not in fullscreen");
    check(fs.wasToolbars && fs.wasPanel, "with nothing captured yet, exit restores both");
    check(fs.zoomWaits == 0 && !fs.zoomFromViewport.isValid(), "no zoom hand-off is pending");
  }

  // ── Keep-zones: measured from the revealed widget, with a floor.
  check(FullscreenController::toolbarKeepBand(0) == 150, "an unmeasurable row falls back to 150px");
  check(FullscreenController::toolbarKeepBand(200) == 224, "tall rows keep 24px of grace below them");
  check(FullscreenController::toolbarKeepBand(100) == 150, "the floor wins over a short row");
  check(FullscreenController::panelKeepBand(1200, 0) == 400, "a hidden panel keeps a third of the window");
  check(FullscreenController::panelKeepBand(1200, 405) == 545, "a wide panel keeps its width + 140px");

  // ── Toolbars, while hidden: only the reveal band opens them.
  {
    FullscreenController fs;
    const int inBand = (FullscreenController::REVEAL_TOP + FullscreenController::REVEAL_BOTTOM) / 2;
    check(fs.wantBars(QPoint(600, inBand), 0), "the cursor in the top band reveals the rows");
    check(!fs.wantBars(QPoint(600, FullscreenController::REVEAL_BOTTOM + 40), 0),
          "below the band, the rows stay away");
    check(!fs.wantBars(QPoint(600, 400), 0), "mid-canvas never reveals the rows");
#ifdef Q_OS_MACOS
    check(!fs.wantBars(QPoint(600, 4), 0), "macOS leaves the system menu bar's strip alone");
#else
    check(fs.wantBars(QPoint(600, 0), 0), "elsewhere the whole top edge is ours");
#endif
  }

  // ── Toolbars, once shown: the wider keep-zone holds them, which is the anti-flicker
  // rule — a cursor still ON the revealed rows must not close them.
  {
    FullscreenController fs;
    fs.barsShown = true;
    check(fs.wantBars(QPoint(600, 190), 200), "the cursor on a 200px-tall row set keeps them open");
    check(!fs.wantBars(QPoint(600, 300), 200), "clear of the rows and their grace, they close");
    check(fs.wantBars(QPoint(600, 140), 0), "unmeasurable rows still keep the 150px floor");
    // The zone a shown set keeps is never narrower than the band that opened it.
    check(fs.wantBars(QPoint(600, FullscreenController::REVEAL_BOTTOM - 1), 0),
          "anything inside the reveal band also keeps them open");
  }

  // ── The panel: a generous strip opens it, then the splitter-drag allowance holds it.
  {
    FullscreenController fs;
    check(fs.wantPanel(QPoint(1199, 400), win, 0), "the right edge reveals the panel");
    check(!fs.wantPanel(QPoint(1160, 400), win, 0), "40px in is already too far from the edge");
    fs.panelShown = true;
    check(fs.wantPanel(QPoint(700, 400), win, 405), "dragging the splitter inward keeps it open");
    check(!fs.wantPanel(QPoint(600, 400), win, 405), "past its width + grace, it hides again");
    check(fs.wantPanel(QPoint(900, 400), win, 0), "a third of the window is the narrow-panel floor");
  }

  // ── A cursor outside the window is not a "left the zone" verdict.
  check(FullscreenController::cursorInside(QPoint(0, 0), win), "the top-left corner is inside");
  check(FullscreenController::cursorInside(QPoint(1200, 800), win), "so is the bottom-right corner");
  check(!FullscreenController::cursorInside(QPoint(-1, 400), win), "a pixel left of the window is out");
  check(!FullscreenController::cursorInside(QPoint(600, 801), win), "and so is one below it");

  // ── The zoom hand-off ratio: entering shrinks the viewport ratio below 1, leaving
  // pushes it above, and anything degenerate or invisible declines the animation.
  {
    const auto enter = FullscreenController::handoffRatio(QSize(800, 600), QSize(1600, 1200));
    check(enter.has_value() && std::abs(*enter - 0.5) < 1e-9,
          "entering fullscreen starts the canvas at half scale");
    const auto leave = FullscreenController::handoffRatio(QSize(1600, 1200), QSize(800, 600));
    check(leave.has_value() && std::abs(*leave - 2.0) < 1e-9, "leaving starts it at double");
    // The smaller axis wins, so the canvas always fits the box it appears to come from.
    const auto skew = FullscreenController::handoffRatio(QSize(800, 1200), QSize(1600, 1200));
    check(skew.has_value() && std::abs(*skew - 0.5) < 1e-9, "the tighter axis sets the ratio");
    check(!FullscreenController::handoffRatio(QSize(1000, 1000), QSize(1000, 1000)),
          "an unchanged viewport is not worth animating");
    check(!FullscreenController::handoffRatio(QSize(1000, 1000), QSize(1005, 1005)),
          "a sub-1% change is below the visible threshold");
    check(!FullscreenController::handoffRatio(QSize(10, 10), QSize(1000, 1000)),
          "an absurd shrink is refused rather than played");
    check(!FullscreenController::handoffRatio(QSize(1000, 1000), QSize(10, 10)),
          "…and so is an absurd growth");
    check(!FullscreenController::handoffRatio(QSize(), QSize(800, 600)),
          "nothing captured means nothing to animate");
    check(!FullscreenController::handoffRatio(QSize(800, 600), QSize(0, 0)),
          "a collapsed viewport never divides by zero");
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
