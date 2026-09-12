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

  // Map every action + icon toolbutton to a shared-icon glyph rasterized in
  // `iconColor` (names mirror browser/js/ui/toolbar.js). Null-guarded.
  void MainWindow::styleActionIcons(bool dark, const QColor& iconColor) {
    iconColor_ = iconColor;
    const int s = kToolIcon;
    // Remember each action's glyph so the menu-bar pass below can re-tint just those
    // without repeating this whole mapping.
    auto set = [&](QAction* a, const char* name) {
      if (!a) return;
      a->setIcon(themedIcon(QString::fromLatin1(name), iconColor, s));
      actionIconNames_.insert(a, QString::fromLatin1(name));
      dangerIcons_.remove(a);
    };
    // Destructive actions carry the NEUTRAL menu glyph, exactly like the browser:
    // its context-menu rows paint every .ctx-icon in --text-muted, and the red of
    // `.danger` buttons (#clear-all-lines, #clear-storage) is the BUTTON's fill,
    // under a white glyph — never a red mark on a plain menu background. The
    // dangerIcons_ membership still drives that fill (styleDangerToolButtons).
    auto setDanger = [&](QAction* a, const char* name) {
      if (!a) return;
      a->setIcon(themedIcon(QString::fromLatin1(name), iconColor, s));
      actionIconNames_.insert(a, QString::fromLatin1(name));
      dangerIcons_.insert(a);
    };
    // File / image
    set(actOpen_, "image");
    set(actOpenAnother_, "external");
    set(actLinks_, "link");
    set(actDescription_, "description");
    set(actKeywords_, "keywords");
    set(actConnect_, "server");
    set(actOpenIn_, "monitor");
    set(actCrop_, "crop");
    set(actRotateLeft_, "rotate-ccw");
    set(actRotateRight_, "rotate-cw");
    set(actCycleFilter_, "image");
    // Drawing / history
    set(actStartDraw_, "play");
    set(actStopDraw_, "stop");
    set(actNewLine_, "plus");
    set(actUndo_, "undo");
    // Toolbar clusters ported from the browser (View / Data / Settings): these were
    // menu-only before, so they had no glyph.
    // Not the trash can: that is "delete the project/file" (actClearProject_ below).
    setDanger(actClearAll_, "eraser");
    set(actDownloadJson_, "file-down");
    set(actCopyLayout_, "clipboard");
    set(actUploadJson_, "file-up");
    setDanger(actClearProject_, "trash");
    set(actSettings_, "palette");
    set(actInfo_, "help");
    set(actRedo_, "redo");
    set(actDeleteLast_, "minus");
    setDanger(actDeleteLine_, "trash");
    set(actDeletePoint_, "x");
    set(actDeselect_, "x");
    // View / zoom
    set(actZoomIn_, "plus");
    set(actZoomOut_, "minus");
    set(actFit_, "fit");
    // Show Points / Show Lines are checkable toggles: leave them icon-less so the menu renders
    // its native check-mark for the on state (browser contextMenu.js parity — a check when shown,
    // nothing when hidden). An icon here would take the check column and mask the on/off state.
    if (actShowPoints_) actShowPoints_->setIcon(QIcon());
    if (actShowLines_) actShowLines_->setIcon(QIcon());
    // Arrow toggles (browser parity): a chevron to collapse the points panel (→, it's on the right)
    // and the toolbars (↑). The panel chevron flips ←/→ with its shown state in refreshActions.
    set(actPanel_, actPanel_ && actPanel_->isChecked() ? "chevron-right" : "chevron-left");
    set(actToolbars_, "chevron-up");   // top-menu (toolbars) show/hide, View menu only
    set(actChat_, "sparkle");          // AI Assistant chat dock (browser sparkle parity)
    // The glyph turns over with the state, and each one's hover moves the way the click
    // will (iconMotion.json maximize / minimize; browser twin: fullscreenLayer.js).
    set(actFullscreen_, fs_.active ? "minimize" : "maximize");
    set(actTooltip_, "message");
    set(actAllowFormulas_, "function");
    set(units_.unitCm, "ruler");
    set(units_.unitIn, "ruler");
    // Incognito: always the mask glyph (browser parity — the browser keeps the same icon and
    // just dims it when disabled). Qt auto-greys the icon for the disabled/locked state, so we
    // don't swap in a separate lock glyph.
    if (actIncognito_) actIncognito_->setIcon(themedIcon("incognito", iconColor, s));
    set(actSettings_, "palette");
    set(actAccent_, "palette");   // Settings section: the accent/visuals popover
    // Project / data
    set(actProjects_, "layers");          // browser projects-btn glyph (layers, not folder)
    set(actNewProject_, "file-text");
    set(actSaveProject_, "save");
    set(actSaveProjectFile_, "save");     // Projects toolbar: Save Project (.stencil)
    set(actOpenProjectFile_, "folder");   // Projects toolbar: Open Project (.stencil)
    set(actStencilLiveSync_, "refresh");  // Projects toolbar: live sync to file
    // The two DESTRUCTIVE ones keep the danger tint set above — a plain set() here ran
    // last and quietly repainted them in the normal icon colour, so the trash that
    // removes your project looked like any other button (browser: .btn-danger red).
    setDanger(actDeleteProjectFile_, "trash");
    setDanger(actClearProject_, "trash");
    set(actSaveSession_, "clipboard");
    set(actDownloadJson_, "file-down");
    set(actUploadJson_, "file-up");
    set(actCopyLayout_, "clipboard");
    set(actPasteLayout_, "paste");
    // actSaveImage_/actCopyImage_ always mean "Current" — the toolbar's own generic glyph,
    // whichever variant they perform (browser parity: exportOptionsMenu.js VARIANT_ICONS —
    // "'current' keeps whichever action icon the caller passes").
    set(actSaveImage_, "download");        // browser save-image glyph (download)
    set(actCopyImage_, "copy");
    set(actSaveImageCurrentRow_, "download");   // "Current"'s own row — same glyph as the primary
    set(actCopyImageCurrentRow_, "copy");
    // The split siblings and the other fixed variants each get their OWN glyph — not the
    // trigger's icon repeated (browser parity: contextMenu.js copyImg-sub/dlImg-sub icons).
    // syncSplitCopyDownloadSlot() is re-run right after this pass (applyTheme) so a theme
    // swap mid-compare doesn't matter — visibility, not icon, is all it still touches here.
    set(actSaveImageSplit_, "compare");
    set(actCopyImageSplit_, "compare");
    set(actCopyImageTint_, "palette");
    set(actSaveImageOriginal_, "image");
    set(actSaveImageTint_, "palette");
    set(actCopyImageOriginal_, "image");
    set(actShareImage_, "share");
    set(actPasteImage_, "paste");
    // Help — matches the browser's icon('help')/icon('gear') pair (js/ui/toolbar.js
    // settings-btn/info-btn): the gear opens Shortcuts, the question mark opens Help.
    set(actInfo_, "help");
    set(actShortcuts_, "gear");
    set(actQuit_, "power");
    // Context-menu extras — the same animated outline glyphs as the toolbar's Line/Rect
    // face (browser contextMenu.js parity: ctx-draw-line/ctx-draw-rect, icon('line')/('rect')).
    set(actDrawLineNow_, "line");
    set(actDrawRectNow_, "rect");
    set(actTheme_, dark ? "sun" : "moon");
    // The theme toggle shows the destination scheme (sun when dark, moon when light),
    // matching the browser's toggle glyph. Through `set`, not setIcon: only a REGISTERED
    // glyph can be re-inked for the button it sits on, and this one is accent-filled now,
    // so it needs the white-on-fill pass like every other filled button, or it keeps the
    // menu's dark glyph on the accent.

    // Toolbuttons that aren't backed by a QAction. The rename ✓/✗ take the SAME chip as the
    // ✎/🎨 they replace — accent-filled, white glyph, one size — rather than a green tick and
    // a red cross in a ghost box: on the fill those colours read as a warning, not as the
    // two halves of one edit (user decision, with a picture). The glyph is the chip's, a
    // size up from the marks it swaps with, so the pair is unmistakable at a glance.
    // Browser-style name affordances: a ✎ rename pencil + a 🎨 colour icon. Their chips are
    // accent-FILLED (theme.cpp QToolButton[nameAffordance="true"]), so both glyphs take
    // the accent's own ink in every state — in the theme's ink one of them read as
    // greyed-out beside its twin. A QIcon is the only way in; QSS cannot
    // recolour one.
    const QColor affordanceInk = themePalette(dark, settings_.accentColor).onAccent;
    const auto affordanceIcon = [&](const char* glyph) {
      // A PLAIN themedIcon, not a composed pixmap: the app-wide icon-motion filter traces a
      // button's glyph back through QIcon::cacheKey (iconSet::iconRequestForKey), and a
      // hand-built pixmap has no entry — so the ✎ never drew itself and the ✗ was never
      // struck through on hover. Nothing to compose any more either: these
      // chips are accent-filled in every state, so the glyph is that ink throughout.
      return themedIcon(glyph, affordanceInk, kNameChipGlyph);
    };
    if (nameBar_.edit) nameBar_.edit->setIcon(affordanceIcon("pencil"));
    if (nameBar_.colorBtn) nameBar_.colorBtn->setIcon(affordanceIcon("palette"));
    if (nameBar_.accept) nameBar_.accept->setIcon(affordanceIcon("check"));
    if (nameBar_.cancel) nameBar_.cancel->setIcon(affordanceIcon("x"));
    // nameBar_.blankColorBtn's icon is a live colour swatch (set in updateProjectTitle), not a themed glyph.
    // Both Draw toggles own their own glyph (support/faceSwap.hpp), so they are repainted
    // through their face — instantly, this is a theme change and not a toggle.
    syncDrawModeFace(canvas_ && canvas_->drawMode() == CanvasWidget::DrawMode::Rect, false);
    styleDangerToolButtons();          // filled-red trash buttons (browser .danger parity)
    restyleContextToggles(iconColor);  // theme-text (not accent) checkbox/radio indicators
  }

  // The glyph colour a TOOLBAR button wants for `act` — its ground's on-colour: the
  // accent's own ink (theme.hpp onAccentInk), or white on the fixed danger red. The
  // neutral glyph the MENUS use would vanish into either fill.
  QColor MainWindow::toolButtonIconColor(QAction* act, const QColor& normal) const {
    for (QToolButton* b : findChildren<QToolButton*>()) {
      if (b->defaultAction() != act) continue;
      const QString fill = b->property("toolFill").toString();
      if (fill.isEmpty()) break;
      if (fill == QLatin1String("danger")) return QColor(Qt::white);
      return themePalette(resolveDark(settings_.themeMode), settings_.accentColor).onAccent;
    }
    return dangerIcons_.contains(act) ? QColor(Qt::white) : normal;
  }
}  // namespace stencil::gui

