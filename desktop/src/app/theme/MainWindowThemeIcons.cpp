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

// MainWindow theming: applyTheme() and the icon restyling passes.

namespace stencil::gui {

  // Glyph names mirror browser/js/ui/toolbar/toolbar.js. Null-guarded.
  void MainWindow::styleActionIcons(bool dark, const QColor& iconColor) {
    this->iconColor = iconColor;
    const int s = TOOL_ICON;
    auto set = [&](QAction* a, const char* name) {
      if (!a) return;
      a->setIcon(themedIcon(QString::fromLatin1(name), iconColor, s));
      actionIconNames.insert(a, QString::fromLatin1(name));
      dangerIcons.remove(a);
    };
    // Destructive actions carry the neutral menu glyph (browser: .ctx-icon is --text-muted);
    // dangerIcons drives the button fill.
    auto setDanger = [&](QAction* a, const char* name) {
      if (!a) return;
      a->setIcon(themedIcon(QString::fromLatin1(name), iconColor, s));
      actionIconNames.insert(a, QString::fromLatin1(name));
      dangerIcons.insert(a);
    };
    set(actOpen, "image");
    set(actOpenAnother, "external");
    set(actLinks, "link");
    set(actDescription, "description");
    set(actKeywords, "keywords");
    set(actConnect, "server");
    set(actOpenIn, "monitor");
    set(actCrop, "crop");
    set(actRotateLeft, "rotate-ccw");
    set(actRotateRight, "rotate-cw");
    set(actCycleFilter, "image");
    set(actStartDraw, "play");
    set(actStopDraw, "stop");
    set(actNewLine, "plus");
    set(actUndo, "undo");
    setDanger(actClearAll, "eraser");
    set(actDownloadJson, "file-down");
    set(actCopyLayout, "clipboard");
    set(actUploadJson, "file-up");
    set(actScript, "script");
    setDanger(actClearProject, "trash");
    set(actSettings, "palette");
    set(actInfo, "help");
    set(actRedo, "redo");
    set(actDeleteLast, "minus");
    setDanger(actDeleteLine, "trash");
    set(actDeletePoint, "x");
    set(actDeselect, "x");
    set(actZoomIn, "plus");
    set(actZoomOut, "minus");
    set(actFit, "fit");
    // Checkable toggles stay icon-less so the menu shows its native check (browser contextMenu.js
    // parity).
    if (actShowPoints) actShowPoints->setIcon(QIcon());
    if (actShowLines) actShowLines->setIcon(QIcon());
    set(actPanel, actPanel && actPanel->isChecked() ? "chevron-right" : "chevron-left");
    set(actToolbars, "chevron-up");   // top-menu (toolbars) show/hide, View menu only
    set(actChat, "sparkle");          // AI Assistant chat dock (browser sparkle parity)
    // Each toggle's hover moves the way the click will (iconMotion.json maximize / minimize;
    // browser: fullscreen/layer.js).
    set(actFullscreen, fs.active ? "minimize" : "maximize");
    set(actTooltip, "message");
    set(actAllowFormulas, "function");
    set(units.unitCm, "ruler");
    set(units.unitIn, "ruler");
    // Always the mask glyph, but REGISTERED like the rest: the ghost toggle's ink flips to
    // on-accent when it lights up, and only a named glyph is repainted for that.
    set(actIncognito, "incognito");
    set(actSettings, "palette");
    set(actAccent, "palette");   // Settings section: the accent/visuals popover
    set(actProjects, "layers");          // browser projects-btn glyph (layers, not folder)
    set(actNewProject, "file-text");
    set(actSaveProject, "save");
    set(actSaveProjectFile, "save");     // Projects toolbar: Save Project (.stencil)
    set(actOpenProjectFile, "folder");   // Projects toolbar: Open Project (.stencil)
    set(actStencilLiveSync, "refresh-cw");  // Projects toolbar: live sync to file
    // The two destructive ones keep the danger tint; a plain set() here would repaint them neutral.
    setDanger(actDeleteProjectFile, "trash");
    setDanger(actClearProject, "trash");
    set(actSaveSession, "clipboard");
    set(actDownloadJson, "file-down");
    set(actUploadJson, "file-up");
    set(actScript, "script");
    set(actCopyLayout, "clipboard");
    set(actPasteLayout, "paste");
    // Always the "Current" glyph, whichever variant they perform (browser: export/optionsMenu.js
    // VARIANT_ICONS).
    set(actSaveImage, "download");        // browser save-image glyph (download)
    set(actCopyImage, "copy");
    set(actSaveImageCurrentRow, "download");   // "Current"'s own row — same glyph as the primary
    set(actCopyImageCurrentRow, "copy");
    // Split siblings and fixed variants each get their own glyph (browser: contextMenu.js copyImg-
    // sub/dlImg-sub).
    set(actSaveImageSplit, "compare");
    set(actCopyImageSplit, "compare");
    set(actCopyImageTint, "palette");
    set(actSaveImageOriginal, "image");
    set(actSaveImageTint, "palette");
    set(actCopyImageOriginal, "image");
    set(actShareImage, "share");
    set(actPasteImage, "paste");
    // Browser pair: the gear opens Shortcuts, the question mark opens Help (js/ui/toolbar.js).
    set(actInfo, "help");
    set(actShortcuts, "gear");
    set(actQuit, "power");
    set(actDrawLineNow, "line");
    set(actDrawRectNow, "rect");
    set(actTheme, dark ? "sun" : "moon");
    // Shows the destination scheme. Through `set`, not setIcon: only a registered glyph gets the
    // white-on-fill pass.

    // Toolbuttons without a QAction. The ✓/✗ take the same accent-filled chip as the ✎/🎨 they
    // replace; a QIcon is the only way in.
    const QColor affordanceInk = themePalette(dark, settings.accentColor).onAccent;
    const auto affordanceIcon = [&](const char* glyph) {
      // A plain themedIcon, not a composed pixmap: the icon-motion filter finds a glyph via
      // QIcon::cacheKey.
      return themedIcon(glyph, affordanceInk, NAME_CHIP_GLYPH);
    };
    if (nameBar.edit) nameBar.edit->setIcon(affordanceIcon("pencil"));
    if (nameBar.colorBtn) nameBar.colorBtn->setIcon(affordanceIcon("palette"));
    if (nameBar.accept) nameBar.accept->setIcon(affordanceIcon("check"));
    if (nameBar.cancel) nameBar.cancel->setIcon(affordanceIcon("x"));
    // blankColorBtn's icon is a live swatch; the Draw toggles repaint through their face
    // (support/faceSwap.hpp).
    syncDrawModeFace(canvas && canvas->getDrawMode() == CanvasWidget::DrawMode::RECT, false);
    styleDangerToolButtons();          // filled-red trash buttons (browser .danger parity)
    restyleContextToggles(iconColor);  // theme-text (not accent) checkbox/radio indicators
  }

  // A toolbar glyph takes its ground's on-colour (theme.hpp onAccentInk, or white on danger red).
  QColor MainWindow::toolButtonIconColor(QAction* act, const QColor& normal) const {
    for (QToolButton* b : findChildren<QToolButton*>()) {
      if (b->defaultAction() != act) continue;
      const QString fill = b->property("toolFill").toString();
      // A ghost toggle takes the accent's ink only while it is ON (app.qss toolGhostBox:checked).
      const bool litGhost = b->property("toolGhostBox").toBool() && act->isChecked() && act->isEnabled();
      if (fill.isEmpty() && !litGhost) break;
      if (fill == QLatin1String("danger")) return QColor(Qt::white);
      return themePalette(resolveDark(settings.themeMode), settings.accentColor).onAccent;
    }
    return dangerIcons.contains(act) ? QColor(Qt::white) : normal;
  }
}  // namespace stencil::gui

