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

// MainWindow's theming: the menu-bar tint and the two Draw toggle faces.

namespace stencil::gui {
  // macOS renders the menu bar in the system appearance: re-tint the actions for it and push app-
  // themed icons back onto the buttons.
  void MainWindow::retintMenuIconsForSystem(bool appDark, const QColor& appIconColor) {
#ifdef Q_OS_MACOS
    const bool sysDark = systemPrefersDark();
    if (sysDark == appDark) return;   // nothing to reconcile
    const QColor menuCol = themePalette(sysDark, settings_.accentColor).textMain;
    const int s = kToolIcon;
    for (auto it = actionIconNames_.constBegin(); it != actionIconNames_.constEnd(); ++it) {
      if (it.key()) it.key()->setIcon(themedIcon(it.value(), menuCol, s));
    }
    // After the actions, since a QToolButton mirrors its default action's icon on every change.
    for (QToolButton* b : findChildren<QToolButton*>()) {
      QAction* a = b->defaultAction();
      if (!a) continue;
      // A toggle that paints its own face (support/faceSwap.hpp) is re-synced below; its glyph
      // colour is its state.
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

  // Start ▶ / Stop ■: the functional half lands at once, the face and accent state cross over
  // through the shared swap. Browser: #draw-toggle / .active.
  void MainWindow::syncDrawToggleFace(bool drawing, bool animate) {
    if (!startDrawBtn_ || !actStartDraw_ || !actStopDraw_) return;
    QAction* want = drawing ? actStopDraw_ : actStartDraw_;
    const bool flipped = startDrawBtn_->defaultAction() != want;
    // A swap already heading for this face owns the button until it lands.
    if (!flipped && animate && faceSwapping(startDrawBtn_)) return;
    if (flipped) startDrawBtn_->setDefaultAction(want);   // icon/tooltip/enabled/click target
    const Palette pal = themePalette(resolveDark(settings_.themeMode), settings_.accentColor);
    FaceSpec face;
    face.glyph = drawing ? QStringLiteral("stop") : QStringLiteral("play");
    face.label = want->iconText();   // the short toolbar word; the menus keep the long one
    face.iconSize = kToolIcon;
    face.gapPx = kFaceIconGap;   // air between glyph and word (see mainWindowHelpers.hpp)
    // Idle: the theme's own ink, the accent outline says draw toggle; running: the fill's own ink.
    face.glyphColor = drawing ? pal.onAccent : pal.textMain;
    face.textColor = face.glyphColor;
    // The fill flip hides at the swap's pivot; it sets the state, so a superseded swap can be
    // dropped.
    auto applyFill = [this, drawing] {
      startDrawBtn_->setProperty("drawToggle", drawing ? QStringLiteral("on")
                                                       : QStringLiteral("idle"));
      startDrawBtn_->style()->unpolish(startDrawBtn_);
      startDrawBtn_->style()->polish(startDrawBtn_);
    };
    swapFace(startDrawBtn_, face, applyFill, animate && flipped ? kFaceSwapMs : 0);
  }

  // Line ✎ / Rect ▭: the same swap with a permanent accent fill, like the browser's bare
  // `<button>` #draw-mode-toggle. Port of drawingApp.js syncDrawModeUI.
  void MainWindow::syncDrawModeFace(bool rect, bool animate) {
    if (!drawModeBtn_) return;
    FaceSpec face;
    // Siblings from the shared canon (browser/js/config/icons.json), so they carry the motion
    // hooks too.
    face.glyph = rect ? QStringLiteral("rect") : QStringLiteral("line");
    face.label = rect ? QStringLiteral("Rect") : QStringLiteral("Line");
    face.iconSize = 16;   // a touch under kToolIcon: this glyph reads heavier than the rest
    face.gapPx = kFaceIconGap;   // …and the same air before the word as its twin
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

