// buildMainToolbar()'s second row — the wrapping tool row, in the browser topbar order. Collapses
// with the Controls pill.
#include "MainWindow.hpp"
#include "ToolbarBuilder.hpp"
#include "mainWindowHelpers.hpp"
#include "SearchCombo.hpp"
#include "OpenImageButton.hpp"
#include "../../support/modal/imageAnchor.hpp"
#include "../../support/control/WrapRow.hpp"
#include "../../support/uiTimings.hpp"
#include "tipContent.hpp"

namespace stencil::gui {

  void ToolbarBuilder::buildToolSectionsRow() {

    // The tool row wraps, so nothing reaches QToolBar's "»".
    auto* tb = w.editor->addToolBar("Main");
    tb->setObjectName("mainToolbar");  // named for QMainWindow::saveState
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->setIconSize(QSize(TOOL_ICON, TOOL_ICON));

    // Blank-background swatch (browser parity), shown only for blank projects; gated in
    // updateProjectTitle.
    w.nameBar.blankColorBtn = new QToolButton(&w);
    // updateColorSwatch gives it the same 46×26 chip as the line-colour chip; labelled "Blank" as
    // in the browser.
    w.nameBar.blankColorBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    w.nameBar.blankColorBtn->setText("Blank");
    w.nameBar.blankColorBtn->setToolTip("Blank background color — recolor this blank image (keeps your lines)");
    w.nameBar.blankColorBtn->setVisible(false);
    QObject::connect(w.nameBar.blankColorBtn, &QToolButton::clicked, &w, [this] { w.parts.projects.setActiveBlankColor(); });

    // Dialog icons answer dblclick / right-click with the compact popover; a plain click is
    // deferred one double-click interval because exec() blocks.
    w.pop.dialogActions = {w.acts.open, w.acts.openAnother, w.acts.openIn, w.acts.projects, w.acts.connect, w.acts.links,
                             w.acts.description, w.acts.keywords, w.acts.chat, w.acts.assistantSettings, w.acts.shortcuts,
                             w.acts.settings, w.acts.info, w.acts.script};
    w.pop.clickTimer = new QTimer(&w);
    w.pop.clickTimer->setSingleShot(true);
    w.pop.clickTimer->setInterval(support::uiTimings().doubleClickMs);
    QObject::connect(w.pop.clickTimer, &QTimer::timeout, &w, [this] {
      if (QAction* act = w.pop.pendingAction.data()) {
        w.pop.pendingAction.clear();
        act->trigger();
      }
    });

    // With no image the cluster is one labelled "Open Image" button; refreshActions swaps in the
    // icon row (browser: #load-image-btn ↔ #open-image-btn).
    w.tools.openImageBtn = new OpenImageButton(&w);
    // The pair's empty-state half, named for modal/imageAnchor.hpp (browser #load-image-btn).
    w.tools.openImageBtn->setObjectName(QLatin1String(OPEN_IMAGE_BTN_NAME));
    w.tools.openImageBtn->setDefaultAction(w.acts.open);
    w.tools.openImageBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    w.tools.openImageBtn->setAutoRaise(true);
    w.tools.openImageBtn->setIconSize(QSize(TOOL_ICON, TOOL_ICON));
    {  // 14px label, matching the browser's #load-image-btn (its default button font).
      QFont f = w.tools.openImageBtn->font();
      f.setPixelSize(14);
      w.tools.openImageBtn->setFont(f);
    }
    // OpenImageButton centres its own icon+label, so no icon-slot reserve to compensate for.
    w.tools.imageSection = makeToolSection("Image",
                                    {w.acts.saveImage, w.acts.copyImage, w.acts.shareImage, w.acts.openIn, w.acts.openAnother,
                                     w.acts.copyProject},
                                    {}, {w.tools.openImageBtn});
    addWrapped(tb, w.tools.imageSection);
    addWrappedSeparator(tb);
    // Description, keywords and links all gate on a saved, non-incognito project
    // (updateProjectTitle).
    addWrapped(tb, makeToolSection("Description & attributes", {w.acts.description, w.acts.keywords, w.acts.links}));
    addWrappedSeparator(tb);
    addWrapped(tb, makeToolSection("Projects", {w.acts.projects, w.acts.saveProjectFile, w.acts.openProjectFile, w.acts.stencilLiveSync, w.acts.deleteProjectFile}));
    addWrappedSeparator(tb);
    w.tools.connectionsSection = makeToolSection("Connections & chat", {w.acts.connect, w.acts.chat});
    addWrapped(tb, w.tools.connectionsSection);
    addWrappedSeparator(tb);
    // Filter combo + tint swatch are built here, wired in buildStyleToolbar; data carries the
    // canonical value.
    w.tools.imageFilter = new SearchComboBox(&w, /*searchable=*/false);
    w.tools.imageFilter->addItem("No Filter", "none");
    w.tools.imageFilter->addItem("B&W", "bw");
    w.tools.imageFilter->addItem("Sepia", "sepia");
    w.tools.imageFilter->addItem("Invert", "invert");
    w.tools.imageFilter->addItem("Contour", "contour");
    w.tools.imageFilter->addItem("Tint", "custom");
    // The browser's #image-filter tooltip, composed and kept current by tipContent.
    setTipBase(w.tools.imageFilter, "Image Filter");
    setTipHotkey(w.tools.imageFilter, w.acts.cycleFilter);
    w.tools.filterColorBtn = new QToolButton(&w);
    w.tools.filterColorBtn->setToolTip("Tint color");
    w.updateColorSwatch(w.tools.filterColorBtn, w.tools.filterColorValue);
    w.tools.filterColorBtn->setVisible(false);   // shown only for the "custom" filter
    addWrapped(tb, makeToolSection("Edit",
                                   {w.acts.crop, w.acts.rotateLeft, w.acts.rotateRight, w.acts.undo, w.acts.redo},
                                   {w.nameBar.blankColorBtn}, {w.tools.imageFilter, w.tools.filterColorBtn}));

    // One Start/Stop button (refreshActions swaps its default action, like the browser's #draw-
    // toggle); the toggle is wired in buildStyleToolbar.
    w.tools.drawModeBtn = new QToolButton(&w);
    w.tools.drawModeBtn->setObjectName("drawFaceBtn");   // theme.cpp: the pair's larger word
    w.tools.drawModeBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    w.tools.drawModeBtn->setText("Line");
    w.tools.drawModeBtn->setAutoRaise(true);
    w.tools.drawModeBtn->setIconSize(QSize(TOOL_ICON + FACE_ICON_GAP, TOOL_ICON));
    setTipBase(w.tools.drawModeBtn, "Drawing mode: Line (click to switch to Rectangle)");
    setTipReason(w.tools.drawModeBtn, "Load an image to switch line / rectangle");   // #draw-mode-toggle
    // Solid accent permanently: no QAction for styleDangerToolButtons to reach, and the browser's
    // #draw-mode-toggle is filled at rest too.
    w.tools.drawModeBtn->setProperty("toolFill", QStringLiteral("accent"));
    // The editable percent combo replaces the browser's +/- steppers; Fit follows it, as in the
    // browser.
    w.tools.zoomFitBtn = new QToolButton(&w);
    w.tools.zoomFitBtn->setProperty("zoomFit", true);
    w.tools.zoomFitBtn->setDefaultAction(w.acts.fit);
    w.tools.zoomFitBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    w.tools.zoomFitBtn->setAutoRaise(true);
    w.tools.zoomFitBtn->setIconSize(QSize(TOOL_ICON, TOOL_ICON));
    // Draw / Zoom / Settings sit on the rows below: all seven sections need ~1230px, and the
    // browser splits them the same way.
  }

}  // namespace stencil::gui
