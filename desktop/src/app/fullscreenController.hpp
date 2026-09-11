#pragma once
#include <QPoint>
#include <QSize>
#include <algorithm>
#include <cmath>
#include <optional>

class QTimer;
class QVariantAnimation;

namespace stencil::gui {

  // Fullscreen mode's own state, plus the two pieces of arithmetic it runs: which
  // edge band counts as a hover reveal, and the scale a fullscreen switch hands the
  // canvas. MainWindow keeps the widget work; nothing here owns a Qt object.
  class FullscreenController {
   public:
    // isFullScreen() is unreliable on macOS, so the mode tracks its own flag.
    bool active = false;
    bool wasToolbars = true;   // toggle states captured on entry, restored on exit
    bool wasPanel = true;
    // Edge-reveal targets. Read these, never isVisible(): during an animated hide the
    // widget stays visible until the slide ends, which would restart the hide each tick.
    bool barsShown = false;
    bool panelShown = false;
    QTimer* hoverTimer = nullptr;         // ~60 Hz cursor poll (parented to the window)
    QVariantAnimation* zoomAnim = nullptr;
    QSize zoomFromViewport;               // viewport size captured before the show/showNormal
    int zoomWaits = 0;                    // ticks spent waiting for the new geometry

    // The reveal band is only consulted while hidden; once shown, the wider keep-zone
    // is. On macOS the top strip belongs to the auto-revealing system menu bar, so the
    // band starts below it and is shorter.
#ifdef Q_OS_MACOS
    static constexpr int kRevealTop = 26, kRevealBottom = 60;
#else
    static constexpr int kRevealTop = 0, kRevealBottom = 120;
#endif
    static constexpr int kPanelRevealPx = 28;

    // Keep-zones are measured from the revealed widgets themselves plus a grace, so a
    // cursor still ON the rows (or mid splitter-drag on the panel) never leaves the zone.
    static int toolbarKeepBand(int toolbarBottom) { return std::max(toolbarBottom + 24, 150); }
    static int panelKeepBand(int winWidth, int panelWidth) {
      return std::max(winWidth / 3, panelWidth + 140);
    }

    // One poll tick's two decisions. `p` is the cursor in window coordinates,
    // `toolbarBottom` the lowest visible tool row's bottom, `panelWidth` the panel's
    // width (0 while hidden). Asked separately because the bars move first and the
    // panel's width is read after that slide has been started.
    bool wantBars(const QPoint& p, int toolbarBottom) const {
      return barsShown ? (p.y() < toolbarKeepBand(toolbarBottom))
                       : (p.y() > kRevealTop && p.y() < kRevealBottom);
    }
    bool wantPanel(const QPoint& p, const QSize& win, int panelWidth) const {
      return panelShown ? (p.x() > win.width() - panelKeepBand(win.width(), panelWidth))
                        : (p.x() > win.width() - kPanelRevealPx);
    }

    // Cursor outside the window: the tick is skipped rather than counted as a leave.
    static bool cursorInside(const QPoint& p, const QSize& win) {
      return p.x() >= 0 && p.y() >= 0 && p.x() <= win.width() && p.y() <= win.height();
    }

    // How much the canvas must appear to grow so the switch reads as one zoom. Empty
    // when the ratio is degenerate or close enough to 1 to be invisible; the caller
    // handles the not-yet-resized case (now == from) by polling again.
    static std::optional<double> handoffRatio(const QSize& from, const QSize& now) {
      if (!from.isValid() || from.isEmpty() || now.isEmpty()) return std::nullopt;
      const double r = std::min(double(from.width()) / now.width(),
                                double(from.height()) / now.height());
      if (r < 0.05 || r > 20.0 || std::abs(r - 1.0) < 0.01) return std::nullopt;
      return r;
    }
    static constexpr int kZoomWaitLimit = 12;   // bounded, so a WM that never resizes just skips
  };

}  // namespace stencil::gui
