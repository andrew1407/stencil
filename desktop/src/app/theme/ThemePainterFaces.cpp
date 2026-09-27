#include "MainWindow.hpp"
#include "ThemePainter.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "../../support/theme/faceSwap.hpp"
#include "theme.hpp"
#include "tipContent.hpp"

// MainWindow's theming: the menu-bar tint and the two Draw toggle faces.

namespace stencil::gui {
  // macOS renders the menu bar in the system appearance: re-tint the actions for it and push app-
  // themed icons back onto the buttons.
  void ThemePainter::retintMenuIconsForSystem(bool appDark, const QColor& appIconColor) {
#ifdef Q_OS_MACOS
    const bool sysDark = systemPrefersDark();
    if (sysDark == appDark) return;   // nothing to reconcile
    const QColor menuCol = themePalette(sysDark, w.settings.accentColor).textMain;
    const int s = TOOL_ICON;
    for (auto it = w.painted.iconNames.constBegin(); it != w.painted.iconNames.constEnd(); ++it) {
      if (it.key()) it.key()->setIcon(themedIcon(it.value(), menuCol, s));
    }
    // After the actions, since a QToolButton mirrors its default action's icon on every change.
    for (QToolButton* b : w.findChildren<QToolButton*>()) {
      QAction* a = b->defaultAction();
      if (!a) continue;
      // A toggle that paints its own face (support/theme/faceSwap.hpp) is re-synced below; its glyph
      // colour is its state.
      if (b->property(FACE_GLYPH_PROPERTY).isValid()) continue;
      const auto name = w.painted.iconNames.constFind(a);
      if (name != w.painted.iconNames.constEnd()) {
        const QColor ink = w.toolButtonIconColor(a, appIconColor);
        b->setIcon(themedIcon(name.value(), ink, s));
      }
    }
    syncDrawToggleFace(w.canvas && w.canvas->getIsDrawing(), false);
#else
    Q_UNUSED(appDark);
    Q_UNUSED(appIconColor);
#endif
  }

  // Start ▶ / Stop ■: the functional half lands at once, the face and accent state cross over
  // through the shared swap. Browser: #draw-toggle / .active.
  void ThemePainter::syncDrawToggleFace(bool drawing, bool animate) {
    if (!w.tools.startDrawBtn || !w.acts.startDraw || !w.acts.stopDraw) return;
    QAction* want = drawing ? w.acts.stopDraw : w.acts.startDraw;
    const bool flipped = w.tools.startDrawBtn->defaultAction() != want;
    // A swap already heading for this face owns the button until it lands.
    if (!flipped && animate && faceSwapping(w.tools.startDrawBtn)) return;
    if (flipped) w.tools.startDrawBtn->setDefaultAction(want);   // icon/tooltip/enabled/click target
    const Palette pal = themePalette(resolveDark(w.settings.themeMode), w.settings.accentColor);
    FaceSpec face;
    face.glyph = drawing ? QStringLiteral("stop") : QStringLiteral("play");
    face.label = want->iconText();   // the short toolbar word; the menus keep the long one
    face.iconSize = TOOL_ICON;
    face.gapPx = FACE_ICON_GAP;   // air between glyph and word (see mainWindowHelpers.hpp)
    // Idle: the theme's own ink, the accent outline says draw toggle; running: the fill's own ink.
    face.glyphColor = drawing ? pal.onAccent : pal.textMain;
    face.textColor = face.glyphColor;
    // The fill flip hides at the swap's pivot; it sets the state, so a superseded swap can be
    // dropped.
    auto applyFill = [this, drawing] {
      w.tools.startDrawBtn->setProperty("drawToggle", drawing ? QStringLiteral("on")
                                                       : QStringLiteral("idle"));
      w.tools.startDrawBtn->style()->unpolish(w.tools.startDrawBtn);
      w.tools.startDrawBtn->style()->polish(w.tools.startDrawBtn);
    };
    swapFace(w.tools.startDrawBtn, face, applyFill, animate && flipped ? FACE_SWAP_MS : 0);
    centreFaceLabel(w.tools.startDrawBtn, FACE_PAD_X_PX);
  }

  // Line ✎ / Rect ▭: the same swap with a permanent accent fill, like the browser's bare
  // `<button>` #draw-mode-toggle. Port of drawingApp.js syncDrawModeUI.
  void ThemePainter::syncDrawModeFace(bool rect, bool animate) {
    if (!w.tools.drawModeBtn) return;
    FaceSpec face;
    // Siblings from the shared canon (browser/js/config/icons.json), so they carry the motion
    // hooks too.
    face.glyph = rect ? QStringLiteral("rect") : QStringLiteral("line");
    face.label = rect ? QStringLiteral("Rect") : QStringLiteral("Line");
    face.iconSize = 16;   // a touch under TOOL_ICON: this glyph reads heavier than the rest
    face.gapPx = FACE_ICON_GAP;   // …and the same air before the word as its twin
    const Palette pal = themePalette(resolveDark(w.settings.themeMode), w.settings.accentColor);
    face.glyphColor = pal.onAccent;
    face.textColor = pal.onAccent;
    setTipBase(w.tools.drawModeBtn, rect ? "Drawing mode: Rectangle (click to switch to Line)"
                                  : "Drawing mode: Line (click to switch to Rectangle)");
    const bool flipped =
        w.tools.drawModeBtn->property(FACE_LABEL_PROPERTY).toString() != face.label;
    swapFace(w.tools.drawModeBtn, face, {}, animate && flipped ? FACE_SWAP_MS : 0);
    centreFaceLabel(w.tools.drawModeBtn, FACE_PAD_X_PX);
  }
}  // namespace stencil::gui

