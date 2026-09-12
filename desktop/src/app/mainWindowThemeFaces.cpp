#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "../support/dockGrip.hpp"
#include "dropZonesOverlay.hpp"
#include "chatDock.hpp"
#include "chatMenuPanel.hpp"
#include "iconSet.hpp"
#include "incognitoOverlay.hpp"
#include "logoHoverFx.hpp"
#include "notifications.hpp"
#include "projectDragZones.hpp"
#include "selectionPanel.hpp"
#include "selectedLineBar.hpp"
#include "theme.hpp"
#include "pillScrollBars.hpp"
#include "tipContent.hpp"
#include "../support/faceSwap.hpp"
#include "../support/motionPrefs.hpp"   // support::dustAllowed()
#include "../support/themeSwapOverlay.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QLayout>
#include <QPainter>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>

// MainWindow's theming: applyTheme() and the icon/section restyling passes.
// Split from mainWindow.cpp; same class, definitions only.

namespace stencil::gui {
  // macOS renders the menu bar in the SYSTEM appearance, so when it disagrees
  // with the app theme, re-tint the ACTIONS for the system and push app-themed
  // icons back onto the toolbar BUTTONS. No-op elsewhere / when they agree.
  void MainWindow::retintMenuIconsForSystem(bool appDark, const QColor& appIconColor) {
#ifdef Q_OS_MACOS
    const bool sysDark = systemPrefersDark();
    if (sysDark == appDark) return;   // nothing to reconcile
    const QColor menuCol = themePalette(sysDark, settings_.accentColor).textMain;
    const int s = kToolIcon;
    // Every menu glyph takes the menu text colour — destructive ones included
    // (their red lives on the toolbar button's fill, see styleActionIcons).
    for (auto it = actionIconNames_.constBegin(); it != actionIconNames_.constEnd(); ++it) {
      if (it.key()) it.key()->setIcon(themedIcon(it.value(), menuCol, s));
    }
    // …then give the toolbar buttons their app-themed icons back. Done AFTER the
    // actions, since a QToolButton mirrors its default action's icon on every change.
    for (QToolButton* b : findChildren<QToolButton*>()) {
      QAction* a = b->defaultAction();
      if (!a) continue;
      // A toggle that paints its own face (support/faceSwap.hpp) is re-synced below, not
      // repainted from the action — its glyph colour is its STATE, not the theme text.
      if (b->property(kFaceGlyphProperty).isValid()) continue;
      const auto name = actionIconNames_.constFind(a);
      if (name != actionIconNames_.constEnd()) {
        const QColor ink = toolButtonIconColor(a, appIconColor);
        b->setIcon(themedIcon(name.value(), ink, s));
      }
    }
    syncDrawToggleFace(canvas_ && canvas_->isDrawing(), false);
#else
    Q_UNUSED(appDark);
    Q_UNUSED(appIconColor);
#endif
  }

  // Start ▶ / Stop ■. The FUNCTIONAL half lands at once — which action a click fires, the
  // tooltip and shortcut it carries, whether it is enabled — while the face (glyph + word)
  // and the accent state cross over through the shared swap. Idle is the OUTLINED accent
  // (accent glyph and word on a neutral face); drawing is the filled one, whose foreground
  // is the app's on-accent ink, exactly as chatDock's filled accent buttons pick theirs.
  // Browser parity: #draw-toggle / .active in layout.css.
  void MainWindow::syncDrawToggleFace(bool drawing, bool animate) {
    if (!startDrawBtn_ || !actStartDraw_ || !actStopDraw_) return;
    QAction* want = drawing ? actStopDraw_ : actStartDraw_;
    const bool flipped = startDrawBtn_->defaultAction() != want;
    // A swap already heading for this face owns the button until it lands — refreshActions
    // runs on all sorts of things, and none of them should cut a toggle short.
    if (!flipped && animate && faceSwapping(startDrawBtn_)) return;
    if (flipped) startDrawBtn_->setDefaultAction(want);   // icon/tooltip/enabled/click target
    const Palette pal = themePalette(resolveDark(settings_.themeMode), settings_.accentColor);
    FaceSpec face;
    face.glyph = drawing ? QStringLiteral("stop") : QStringLiteral("play");
    face.label = want->iconText();   // the short toolbar word; the menus keep the long one
    face.iconSize = kToolIcon;
    face.gapPx = kFaceIconGap;   // air between glyph and word (see mainWindowHelpers.hpp)
    // Idle: the theme's own ink, as every other label and glyph in the row (user decision) —
    // the accent OUTLINE is what says this is the draw toggle. Running: the fill's own ink.
    face.glyphColor = drawing ? pal.onAccent : pal.textMain;
    face.textColor = face.glyphColor;
    // The fill flip is hidden at the swap's pivot, where the face is invisible. It SETS the
    // state (never toggles it), so a superseded swap can be dropped without stranding it.
    auto applyFill = [this, drawing] {
      startDrawBtn_->setProperty("drawToggle", drawing ? QStringLiteral("on")
                                                       : QStringLiteral("idle"));
      startDrawBtn_->style()->unpolish(startDrawBtn_);
      startDrawBtn_->style()->polish(startDrawBtn_);
    };
    swapFace(startDrawBtn_, face, applyFill, animate && flipped ? kFaceSwapMs : 0);
  }

  // Line ✎ / Rect ▭ — the same swap, no accent STATE of its own (it picks the mode, it
  // does not report a live session) but a permanent accent FILL: it has no idle/on pair
  // to distinguish the way Start/Stop does, and the plain toolbutton ghost (transparent,
  // a translucent tint only on hover) left it looking unstyled next to every other
  // action button in the row, which the browser's `#draw-mode-toggle` never is — a bare
  // `<button>` there, so it is solid accent-filled at rest too (layout.css `button {}`).
  // Port of drawingApp.js syncDrawModeUI.
  void MainWindow::syncDrawModeFace(bool rect, bool animate) {
    if (!drawModeBtn_) return;
    FaceSpec face;
    // SIBLINGS, not two families: an outlined rectangle beside a line-with-endpoint-dots,
    // both 2px strokes on the same grid, both anchoring the SAME two drag handles. Both
    // now live in the shared canon (browser/js/config/icons.json), so they carry the
    // motion hooks too and draw themselves on hover (iconMotion.json).
    face.glyph = rect ? QStringLiteral("rect") : QStringLiteral("line");
    face.label = rect ? QStringLiteral("Rect") : QStringLiteral("Line");
    face.iconSize = 16;   // a touch under kToolIcon: this glyph reads heavier than the rest
    face.gapPx = kFaceIconGap;   // …and the same air before the word as its twin
    // The accent's own ink, like every other filled toolbar button (toolButtonIconColor).
    const Palette pal = themePalette(resolveDark(settings_.themeMode), settings_.accentColor);
    face.glyphColor = pal.onAccent;
    face.textColor = pal.onAccent;
    setTipBase(drawModeBtn_, rect ? "Drawing mode: Rectangle (click to switch to Line)"
                                  : "Drawing mode: Line (click to switch to Rectangle)");
    const bool flipped =
        drawModeBtn_->property(kFaceLabelProperty).toString() != face.label;
    swapFace(drawModeBtn_, face, {}, animate && flipped ? kFaceSwapMs : 0);
  }
}  // namespace stencil::gui

