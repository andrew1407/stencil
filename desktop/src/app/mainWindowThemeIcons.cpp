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

// MainWindow theming: applyTheme() and the icon restyling passes.

namespace stencil::gui {

  // Glyph names mirror browser/js/ui/toolbar.js. Null-guarded.
  void MainWindow::styleActionIcons(bool dark, const QColor& iconColor) {
    iconColor_ = iconColor;
    const int s = TOOL_ICON;
    auto set = [&](QAction* a, const char* name) {
      if (!a) return;
      a->setIcon(themedIcon(QString::fromLatin1(name), iconColor, s));
      actionIconNames_.insert(a, QString::fromLatin1(name));
      dangerIcons_.remove(a);
    };
    // Destructive actions carry the neutral menu glyph (browser: .ctx-icon is --text-muted);
    // dangerIcons_ drives the button fill.
    auto setDanger = [&](QAction* a, const char* name) {
      if (!a) return;
      a->setIcon(themedIcon(QString::fromLatin1(name), iconColor, s));
      actionIconNames_.insert(a, QString::fromLatin1(name));
      dangerIcons_.insert(a);
    };
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
    set(actStartDraw_, "play");
    set(actStopDraw_, "stop");
    set(actNewLine_, "plus");
    set(actUndo_, "undo");
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
    set(actZoomIn_, "plus");
    set(actZoomOut_, "minus");
    set(actFit_, "fit");
    // Checkable toggles stay icon-less so the menu shows its native check (browser contextMenu.js
    // parity).
    if (actShowPoints_) actShowPoints_->setIcon(QIcon());
    if (actShowLines_) actShowLines_->setIcon(QIcon());
    set(actPanel_, actPanel_ && actPanel_->isChecked() ? "chevron-right" : "chevron-left");
    set(actToolbars_, "chevron-up");   // top-menu (toolbars) show/hide, View menu only
    set(actChat_, "sparkle");          // AI Assistant chat dock (browser sparkle parity)
    // Each toggle's hover moves the way the click will (iconMotion.json maximize / minimize;
    // browser: fullscreenLayer.js).
    set(actFullscreen_, fs_.active ? "minimize" : "maximize");
    set(actTooltip_, "message");
    set(actAllowFormulas_, "function");
    set(units_.unitCm, "ruler");
    set(units_.unitIn, "ruler");
    // Always the mask glyph; Qt greys it when disabled (browser parity).
    if (actIncognito_) actIncognito_->setIcon(themedIcon("incognito", iconColor, s));
    set(actSettings_, "palette");
    set(actAccent_, "palette");   // Settings section: the accent/visuals popover
    set(actProjects_, "layers");          // browser projects-btn glyph (layers, not folder)
    set(actNewProject_, "file-text");
    set(actSaveProject_, "save");
    set(actSaveProjectFile_, "save");     // Projects toolbar: Save Project (.stencil)
    set(actOpenProjectFile_, "folder");   // Projects toolbar: Open Project (.stencil)
    set(actStencilLiveSync_, "refresh");  // Projects toolbar: live sync to file
    // The two destructive ones keep the danger tint; a plain set() here would repaint them neutral.
    setDanger(actDeleteProjectFile_, "trash");
    setDanger(actClearProject_, "trash");
    set(actSaveSession_, "clipboard");
    set(actDownloadJson_, "file-down");
    set(actUploadJson_, "file-up");
    set(actCopyLayout_, "clipboard");
    set(actPasteLayout_, "paste");
    // Always the "Current" glyph, whichever variant they perform (browser: exportOptionsMenu.js
    // VARIANT_ICONS).
    set(actSaveImage_, "download");        // browser save-image glyph (download)
    set(actCopyImage_, "copy");
    set(actSaveImageCurrentRow_, "download");   // "Current"'s own row — same glyph as the primary
    set(actCopyImageCurrentRow_, "copy");
    // Split siblings and fixed variants each get their own glyph (browser: contextMenu.js copyImg-
    // sub/dlImg-sub).
    set(actSaveImageSplit_, "compare");
    set(actCopyImageSplit_, "compare");
    set(actCopyImageTint_, "palette");
    set(actSaveImageOriginal_, "image");
    set(actSaveImageTint_, "palette");
    set(actCopyImageOriginal_, "image");
    set(actShareImage_, "share");
    set(actPasteImage_, "paste");
    // Browser pair: the gear opens Shortcuts, the question mark opens Help (js/ui/toolbar.js).
    set(actInfo_, "help");
    set(actShortcuts_, "gear");
    set(actQuit_, "power");
    set(actDrawLineNow_, "line");
    set(actDrawRectNow_, "rect");
    set(actTheme_, dark ? "sun" : "moon");
    // Shows the destination scheme. Through `set`, not setIcon: only a registered glyph gets the
    // white-on-fill pass.

    // Toolbuttons without a QAction. The ✓/✗ take the same accent-filled chip as the ✎/🎨 they
    // replace; a QIcon is the only way in.
    const QColor affordanceInk = themePalette(dark, settings_.accentColor).onAccent;
    const auto affordanceIcon = [&](const char* glyph) {
      // A plain themedIcon, not a composed pixmap: the icon-motion filter finds a glyph via
      // QIcon::cacheKey.
      return themedIcon(glyph, affordanceInk, NAME_CHIP_GLYPH);
    };
    if (nameBar_.edit) nameBar_.edit->setIcon(affordanceIcon("pencil"));
    if (nameBar_.colorBtn) nameBar_.colorBtn->setIcon(affordanceIcon("palette"));
    if (nameBar_.accept) nameBar_.accept->setIcon(affordanceIcon("check"));
    if (nameBar_.cancel) nameBar_.cancel->setIcon(affordanceIcon("x"));
    // blankColorBtn's icon is a live swatch; the Draw toggles repaint through their face
    // (support/faceSwap.hpp).
    syncDrawModeFace(canvas_ && canvas_->drawMode() == CanvasWidget::DrawMode::RECT, false);
    styleDangerToolButtons();          // filled-red trash buttons (browser .danger parity)
    restyleContextToggles(iconColor);  // theme-text (not accent) checkbox/radio indicators
  }

  // A toolbar glyph takes its ground's on-colour (theme.hpp onAccentInk, or white on danger red).
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

