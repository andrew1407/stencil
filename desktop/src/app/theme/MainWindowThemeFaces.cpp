#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "../../support/dockGrip.hpp"
#include "DropZonesOverlay.hpp"
#include "ChatDock.hpp"
#include "ChatMenuPanel.hpp"
#include "iconSet.hpp"
#include "IncognitoOverlay.hpp"
#include "LogoHoverFx.hpp"
#include "Notifications.hpp"
#include "ProjectDragZones.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "theme.hpp"
#include "PillScrollBars.hpp"
#include "tipContent.hpp"
#include "../../support/theme/faceSwap.hpp"
#include "../../support/motionPrefs.hpp"   // support::isDustAllowed()
#include "../../support/dust/ThemeSwapOverlay.hpp"

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
    const QColor menuCol = themePalette(sysDark, settings.accentColor).textMain;
    const int s = TOOL_ICON;
    for (auto it = actionIconNames.constBegin(); it != actionIconNames.constEnd(); ++it) {
      if (it.key()) it.key()->setIcon(themedIcon(it.value(), menuCol, s));
    }
    // After the actions, since a QToolButton mirrors its default action's icon on every change.
    for (QToolButton* b : findChildren<QToolButton*>()) {
      QAction* a = b->defaultAction();
      if (!a) continue;
      // A toggle that paints its own face (support/faceSwap.hpp) is re-synced below; its glyph
      // colour is its state.
      if (b->property(FACE_GLYPH_PROPERTY).isValid()) continue;
      const auto name = actionIconNames.constFind(a);
      if (name != actionIconNames.constEnd()) {
        const QColor ink = toolButtonIconColor(a, appIconColor);
        b->setIcon(themedIcon(name.value(), ink, s));
      }
    }
    syncDrawToggleFace(canvas && canvas->getIsDrawing(), false);
#else
    Q_UNUSED(appDark);
    Q_UNUSED(appIconColor);
#endif
  }

  // Start ▶ / Stop ■: the functional half lands at once, the face and accent state cross over
  // through the shared swap. Browser: #draw-toggle / .active.
  void MainWindow::syncDrawToggleFace(bool drawing, bool animate) {
    if (!startDrawBtn || !actStartDraw || !actStopDraw) return;
    QAction* want = drawing ? actStopDraw : actStartDraw;
    const bool flipped = startDrawBtn->defaultAction() != want;
    // A swap already heading for this face owns the button until it lands.
    if (!flipped && animate && faceSwapping(startDrawBtn)) return;
    if (flipped) startDrawBtn->setDefaultAction(want);   // icon/tooltip/enabled/click target
    const Palette pal = themePalette(resolveDark(settings.themeMode), settings.accentColor);
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
      startDrawBtn->setProperty("drawToggle", drawing ? QStringLiteral("on")
                                                       : QStringLiteral("idle"));
      startDrawBtn->style()->unpolish(startDrawBtn);
      startDrawBtn->style()->polish(startDrawBtn);
    };
    swapFace(startDrawBtn, face, applyFill, animate && flipped ? FACE_SWAP_MS : 0);
  }

  // Line ✎ / Rect ▭: the same swap with a permanent accent fill, like the browser's bare
  // `<button>` #draw-mode-toggle. Port of drawingApp.js syncDrawModeUI.
  void MainWindow::syncDrawModeFace(bool rect, bool animate) {
    if (!drawModeBtn) return;
    FaceSpec face;
    // Siblings from the shared canon (browser/js/config/icons.json), so they carry the motion
    // hooks too.
    face.glyph = rect ? QStringLiteral("rect") : QStringLiteral("line");
    face.label = rect ? QStringLiteral("Rect") : QStringLiteral("Line");
    face.iconSize = 16;   // a touch under TOOL_ICON: this glyph reads heavier than the rest
    face.gapPx = FACE_ICON_GAP;   // …and the same air before the word as its twin
    const Palette pal = themePalette(resolveDark(settings.themeMode), settings.accentColor);
    face.glyphColor = pal.onAccent;
    face.textColor = pal.onAccent;
    setTipBase(drawModeBtn, rect ? "Drawing mode: Rectangle (click to switch to Line)"
                                  : "Drawing mode: Line (click to switch to Rectangle)");
    const bool flipped =
        drawModeBtn->property(FACE_LABEL_PROPERTY).toString() != face.label;
    swapFace(drawModeBtn, face, {}, animate && flipped ? FACE_SWAP_MS : 0);
  }
}  // namespace stencil::gui

