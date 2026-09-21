#pragma once
#include <QPoint>
#include <QSize>
#include <algorithm>
#include <cmath>
#include <optional>

class QTimer;
class QVariantAnimation;

namespace stencil::gui {

  // Fullscreen state plus its edge-band and scale arithmetic; nothing here owns a Qt object.
  class FullscreenController {
   public:
    // isFullScreen() is unreliable on macOS, so the mode tracks its own flag.
    bool active = false;
    bool wasToolbars = true;   // toggle states captured on entry, restored on exit
    bool wasPanel = true;
    // Read these, never isVisible(): an animated hide stays visible until the slide ends.
    bool barsShown = false;
    bool panelShown = false;
    QTimer* hoverTimer = nullptr;         // ~60 Hz cursor poll (parented to the window)
    QVariantAnimation* zoomAnim = nullptr;
    QSize zoomFromViewport;               // viewport size captured before the show/showNormal
    int zoomWaits = 0;                    // ticks spent waiting for the new geometry

    // On macOS the top strip belongs to the auto-revealing system menu bar, so the band starts below it.
#ifdef Q_OS_MACOS
    static constexpr int REVEAL_TOP = 26, REVEAL_BOTTOM = 60;
#else
    static constexpr int REVEAL_TOP = 0, REVEAL_BOTTOM = 120;
#endif
    static constexpr int PANEL_REVEAL_PX = 28;

    // Keep-zones are the revealed widgets plus a grace, so a cursor still ON them never leaves.
    static int toolbarKeepBand(int toolbarBottom) { return std::max(toolbarBottom + 24, 150); }
    static int panelKeepBand(int winWidth, int panelWidth) {
      return std::max(winWidth / 3, panelWidth + 140);
    }

    // `p` in window coordinates; `panelWidth` 0 while hidden. Asked separately because the bars move first.
    // The revealed panel's tab row sits inside the band, and the rows would push it out from under the cursor
    // (the browser's strip is an overlay): from over the panel, the rows never start to reveal.
    bool wantBars(const QPoint& p, int toolbarBottom, bool isOverPanel = false) const {
      if (!barsShown && isOverPanel) return false;
      return barsShown ? (p.y() < toolbarKeepBand(toolbarBottom))
                       : (p.y() > REVEAL_TOP && p.y() < REVEAL_BOTTOM);
    }
    bool wantPanel(const QPoint& p, const QSize& win, int panelWidth) const {
      return panelShown ? (p.x() > win.width() - panelKeepBand(win.width(), panelWidth))
                        : (p.x() > win.width() - PANEL_REVEAL_PX);
    }

    // Cursor outside the window: skipped, not counted as a leave.
    static bool cursorInside(const QPoint& p, const QSize& win) {
      return p.x() >= 0 && p.y() >= 0 && p.x() <= win.width() && p.y() <= win.height();
    }

    // Empty when the ratio is degenerate or invisibly close to 1; the caller polls again for now == from.
    static std::optional<double> handoffRatio(const QSize& from, const QSize& now) {
      if (!from.isValid() || from.isEmpty() || now.isEmpty()) return std::nullopt;
      const double r = std::min(double(from.width()) / now.width(),
                                double(from.height()) / now.height());
      if (r < 0.05 || r > 20.0 || std::abs(r - 1.0) < 0.01) return std::nullopt;
      return r;
    }
    static constexpr int ZOOM_WAIT_LIMIT = 12;   // bounded, so a WM that never resizes just skips
  };

}  // namespace stencil::gui
