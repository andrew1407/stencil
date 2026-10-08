#include "MainWindow.hpp"
#include "ThemePainter.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "iconMotion.hpp"
#include "iconSet.hpp"
#include "skinPrefs.hpp"
#include "theme.hpp"

// MainWindow theming: applyTheme() and the icon restyling passes.

namespace stencil::gui {

  // Glyph names mirror browser/js/ui/toolbar/toolbar.js. Null-guarded.
  void ThemePainter::styleActionIcons(bool dark, const QColor& iconColor) {
    w.painted.iconColor = iconColor;
    const int s = TOOL_ICON;
    auto set = [&](QAction* a, const char* name) {
      if (!a) return;
      a->setIcon(themedIcon(QString::fromLatin1(name), iconColor, s));
      w.painted.iconNames.insert(a, QString::fromLatin1(name));
      w.tools.dangerIcons.remove(a);
    };
    // Destructive actions carry the neutral menu glyph (browser: .ctx-icon is --text-muted);
    // dangerIcons drives the button fill.
    auto setDanger = [&](QAction* a, const char* name) {
      if (!a) return;
      a->setIcon(themedIcon(QString::fromLatin1(name), iconColor, s));
      w.painted.iconNames.insert(a, QString::fromLatin1(name));
      w.tools.dangerIcons.insert(a);
    };
    set(w.acts.open, "image");
    set(w.acts.openAnother, "external");
    set(w.acts.links, "link");
    set(w.acts.description, "description");
    set(w.acts.keywords, "keywords");
    set(w.acts.connect, "server");
    set(w.acts.openIn, "monitor");
    set(w.acts.copyProject, "duplicate");
    set(w.acts.crop, "crop");
    set(w.acts.rotateLeft, "rotate-ccw");
    set(w.acts.rotateRight, "rotate-cw");
    set(w.acts.flipImage, "flip-horizontal");
    set(w.acts.cycleFilter, "image");
    set(w.acts.cycleFilterPrev, "image");
    set(w.acts.startDraw, "play");
    set(w.acts.stopDraw, "stop");
    set(w.acts.newLine, "plus");
    set(w.acts.undo, "undo");
    setDanger(w.acts.clearAll, "eraser");
    set(w.acts.downloadJson, "file-down");
    set(w.acts.copyLayout, "clipboard");
    set(w.acts.uploadJson, "file-up");
    set(w.acts.script, "script");
    setDanger(w.acts.clearProject, "trash");
    set(w.acts.settings, "palette");
    set(w.acts.info, "help");
    set(w.acts.redo, "redo");
    set(w.acts.deleteLast, "minus");
    setDanger(w.acts.deleteLine, "trash");
    set(w.acts.deletePoint, "x");
    set(w.acts.deselect, "x");
    set(w.acts.zoomIn, "plus");
    set(w.acts.zoomOut, "minus");
    set(w.acts.fit, "fit");
    // The tick is the row's icon while checked (browser .ctx-check), so it plays the check motion on
    // hover; webcore keeps its pixel QMenu::indicator tick.
    for (QAction* a : {w.acts.showPoints, w.acts.showLines}) {
      if (!a) continue;
      const auto tick = [this, a] {
        if (ActionIconMotionRunner* r = icm::runnerOfAction(a)) delete r;
        a->setIcon(a->isChecked() && !support::isWebcore()
                       ? themedIcon(QStringLiteral("check"), w.painted.iconColor, TOOL_ICON) : QIcon());
      };
      tick();
      if (!a->property("tickSync").toBool()) {
        a->setProperty("tickSync", true);
        QObject::connect(a, &QAction::toggled, &w, tick);
      }
    }
    set(w.acts.panel, w.acts.panel && w.acts.panel->isChecked() ? "chevron-right" : "chevron-left");
    set(w.acts.toolbars, "chevron-up");   // top-menu (toolbars) show/hide, View menu only
    set(w.acts.chat, "sparkle");          // AI Assistant chat dock (browser sparkle parity)
    // Each toggle's hover moves the way the click will (iconMotion.json maximize / minimize;
    // browser: fullscreen/layer.js).
    set(w.acts.fullscreen, w.fs.active ? "minimize" : "maximize");
    set(w.acts.tooltip, "message");
    set(w.acts.allowFormulas, "function");
    set(w.units.unitCm, "ruler");
    set(w.units.unitIn, "ruler");
    // Always the mask glyph, but REGISTERED like the rest: the ghost toggle's ink flips to
    // on-accent when it lights up, and only a named glyph is repainted for that.
    set(w.acts.incognito, "incognito");
    set(w.acts.settings, "palette");
    set(w.acts.accent, "palette");   // Settings section: the accent/visuals popover
    set(w.acts.projects, "layers");          // browser projects-btn glyph (layers, not folder)
    set(w.acts.newProject, "file-text");
    set(w.acts.saveProject, "save");
    set(w.acts.saveProjectFile, "save");     // Projects toolbar: Save Project (.stencil)
    set(w.acts.openProjectFile, "folder");   // Projects toolbar: Open Project (.stencil)
    set(w.acts.stencilLiveSync, "refresh-cw");  // Projects toolbar: live sync to file
    // The two destructive ones keep the danger tint; a plain set() here would repaint them neutral.
    setDanger(w.acts.deleteProjectFile, "trash");
    setDanger(w.acts.clearProject, "trash");
    set(w.acts.closeProject, "x");           // closes, removes nothing: no danger tint
    set(w.acts.saveSession, "clipboard");
    set(w.acts.downloadJson, "file-down");
    set(w.acts.uploadJson, "file-up");
    set(w.acts.script, "script");
    set(w.acts.copyLayout, "clipboard");
    set(w.acts.pasteLayout, "paste");
    // Always the "Current" glyph, whichever variant they perform (browser: export/optionsMenu.js
    // VARIANT_ICONS).
    set(w.acts.saveImage, "download");        // browser save-image glyph (download)
    set(w.acts.copyImage, "copy");
    set(w.acts.saveImageCurrentRow, "download");   // "Current"'s own row — same glyph as the primary
    set(w.acts.copyImageCurrentRow, "copy");
    // Split siblings and fixed variants each get their own glyph (browser: contextMenu.js copyImg-
    // sub/dlImg-sub).
    set(w.acts.saveImageSplit, "compare");
    set(w.acts.copyImageSplit, "compare");
    set(w.acts.copyImageTint, "palette");
    set(w.acts.saveImageOriginal, "image");
    set(w.acts.saveImageTint, "palette");
    set(w.acts.copyImageOriginal, "image");
    set(w.acts.shareImage, "share");
    set(w.acts.pasteImage, "paste");
    // Browser pair: the gear opens Shortcuts, the question mark opens Help (browser/js/ui/toolbar/toolbar.js).
    set(w.acts.info, "help");
    set(w.acts.shortcuts, "gear");
    set(w.acts.quit, "power");
    set(w.ctxMenu.drawLineNow, "line");
    set(w.ctxMenu.drawRectNow, "rect");
    set(w.acts.theme, dark ? "sun" : "moon");
    // Shows the destination scheme. Through `set`, not setIcon: only a registered glyph gets the
    // white-on-fill pass.

    // Toolbuttons without a QAction. The ✓/✗ take the same accent-filled chip as the ✎/🎨 they
    // replace; a QIcon is the only way in.
    const QColor affordanceInk = themePalette(dark, w.settings.accentColor).onAccent;
    const auto affordanceIcon = [&](const char* glyph) {
      // A plain themedIcon, not a composed pixmap: the icon-motion filter finds a glyph via
      // QIcon::cacheKey.
      return themedIcon(glyph, affordanceInk, NAME_CHIP_GLYPH);
    };
    if (w.nameBar.edit) w.nameBar.edit->setIcon(affordanceIcon("pencil"));
    if (w.nameBar.colorBtn) w.nameBar.colorBtn->setIcon(affordanceIcon("palette"));
    if (w.nameBar.accept) w.nameBar.accept->setIcon(affordanceIcon("check"));
    if (w.nameBar.cancel) w.nameBar.cancel->setIcon(affordanceIcon("x"));
    // blankColorBtn's icon is a live swatch; the Draw toggles repaint through their face
    // (support/theme/faceSwap.hpp).
    syncDrawModeFace(w.canvas && w.canvas->getDrawMode() == CanvasWidget::DrawMode::RECT, false);
    styleDangerToolButtons();          // filled-red trash buttons (browser .danger parity)
    restyleContextToggles(iconColor);  // theme-text (not accent) checkbox/radio indicators
  }

}  // namespace stencil::gui
