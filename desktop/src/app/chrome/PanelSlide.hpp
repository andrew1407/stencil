#pragma once
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "../../support/uiTimings.hpp"

class QVariantAnimation;

namespace stencil::gui {

  // The desktop's fold clocks, ms (the coordinate panel, the toolbar fold, the Controls pill spin):
  // motion.json's PANEL_* keys through the qrc, as the browser's panel/coordFold.js reads them.
  struct PanelFoldClocks {
    int slideInMs = 420;    // PANEL_FOLD_IN_MS: the toolbar fold and the Controls pill spin, opening
    int slideOutMs = 630;   // PANEL_FOLD_OUT_MS: …closing
    int panelInMs = 470;    // PANEL_SLIDE_IN_MS: the panel's width slide and chevron spin, opening
    int panelOutMs = 470;   // PANEL_SLIDE_OUT_MS: …closing
    int dustInMs = 450;     // PANEL_DUST_IN_MS: the grains gathering into the panel
    int dustOutMs = 390;    // PANEL_DUST_OUT_MS: …leaving it
  };

  inline const PanelFoldClocks& panelFoldClocks() {
    static const PanelFoldClocks c = [] {
      PanelFoldClocks out;
      QFile f(QStringLiteral(":/config/motion.json"));
      if (!f.open(QIODevice::ReadOnly)) return out;
      const QJsonObject ui = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("ui")).toObject();
      const auto ms = [&ui](const char* key, int fallback) {
        const int v = ui.value(QLatin1String(key)).toInt(0);
        return v > 0 ? v : fallback;
      };
      out.slideInMs = ms("PANEL_FOLD_IN_MS", out.slideInMs);
      out.slideOutMs = ms("PANEL_FOLD_OUT_MS", out.slideOutMs);
      out.panelInMs = ms("PANEL_SLIDE_IN_MS", out.panelInMs);
      out.panelOutMs = ms("PANEL_SLIDE_OUT_MS", out.panelOutMs);
      out.dustInMs = ms("PANEL_DUST_IN_MS", out.dustInMs);
      out.dustOutMs = ms("PANEL_DUST_OUT_MS", out.dustOutMs);
      return out;
    }();
    return c;
  }

  // The panel slide's curve, an OutSine: the browser's --fold-ease (css/animations/collapse.css).
  inline QEasingCurve panelSlideEase() { return support::cubicBezier(0.39, 0.575, 0.565, 1.0); }

  // The coordinate panel's show/hide slide: the width it comes back at, and the slide in flight.
  struct PanelSlide {
    // The card's gap to the window's right edge, as wide as the canvas' own left inset.
    static constexpr int EDGE_GAP = 6;
    // The browser's --coord-panel-default (css/layout/coord/panel.css) for the card, plus the gap.
    static constexpr int DEFAULT_WIDTH = 405 + EDGE_GAP;
    // The coordinate columns elide below this; browser .coordinates-panel has the same floor.
    static constexpr int MIN_WIDTH = 240 + EDGE_GAP;
    int restoreWidth = DEFAULT_WIDTH;
    QVariantAnimation* anim = nullptr;
  };

}  // namespace stencil::gui
