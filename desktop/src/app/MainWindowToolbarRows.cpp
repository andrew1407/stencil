// buildMainToolbar()'s second row — the wrapping tool row, in the browser topbar order. Collapses
// with the Controls pill.
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "iconSet.hpp"
#include "LogoHoverFx.hpp"
#include "ControlsPill.hpp"
#include "SearchCombo.hpp"
#include "OpenImageButton.hpp"
#include "theme.hpp"
#include "../support/controlReveal.hpp"
#include "../support/iconMotion.hpp"
#include "../support/ShimmerOverlay.hpp"
#include "../support/WrapRow.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace stencil::gui {

  void MainWindow::buildToolSectionsRow() {

    // The tool row wraps, so nothing reaches QToolBar's "»".
    auto* tb = addToolBar("Main");
    tb->setObjectName("mainToolbar");  // named for QMainWindow::saveState
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->setIconSize(QSize(TOOL_ICON, TOOL_ICON));

    // Blank-background swatch (browser parity), shown only for blank projects; gated in
    // updateProjectTitle.
    nameBar.blankColorBtn = new QToolButton(this);
    // updateColorSwatch gives it the same 46×26 chip as the line-colour chip; labelled "Blank" as
    // in the browser.
    nameBar.blankColorBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    nameBar.blankColorBtn->setText("Blank");
    nameBar.blankColorBtn->setToolTip("Blank background color — recolor this blank image (keeps your lines)");
    nameBar.blankColorBtn->setVisible(false);
    connect(nameBar.blankColorBtn, &QToolButton::clicked, this, [this] { setActiveBlankColor(); });

    // Dialog icons answer dblclick / right-click with the compact popover; a plain click is
    // deferred one double-click interval because exec() blocks.
    pop.dialogActions = {actOpen, actOpenAnother, actOpenIn, actProjects, actConnect, actLinks,
                             actDescription, actKeywords, actChat, actAssistantSettings, actShortcuts,
                             actSettings, actInfo, actScript};
    pop.clickTimer = new QTimer(this);
    pop.clickTimer->setSingleShot(true);
    pop.clickTimer->setInterval(250);
    connect(pop.clickTimer, &QTimer::timeout, this, [this] {
      if (QAction* act = pop.pendingAction.data()) {
        pop.pendingAction.clear();
        act->trigger();
      }
    });

    // With no image the cluster is one labelled "Open Image" button; refreshActions swaps in the
    // icon row (browser: #load-image-btn ↔ #open-image-btn).
    openImageBtn = new OpenImageButton(this);
    openImageBtn->setDefaultAction(actOpen);
    openImageBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    openImageBtn->setAutoRaise(true);
    openImageBtn->setIconSize(QSize(TOOL_ICON, TOOL_ICON));
    {  // 14px label, matching the browser's #load-image-btn (its default button font).
      QFont f = openImageBtn->font();
      f.setPixelSize(14);
      openImageBtn->setFont(f);
    }
    // OpenImageButton centres its own icon+label, so no icon-slot reserve to compensate for.
    imageSection = makeToolSection("Image",
                                    {actSaveImage, actCopyImage, actShareImage, actOpenIn, actOpenAnother},
                                    {}, {openImageBtn});
    addWrapped(tb, imageSection);
    addWrappedSeparator(tb);
    // Description, keywords and links all gate on a saved, non-incognito project
    // (updateProjectTitle).
    addWrapped(tb, makeToolSection("Description & attributes", {actDescription, actKeywords, actLinks}));
    addWrappedSeparator(tb);
    addWrapped(tb, makeToolSection("Projects", {actProjects, actSaveProjectFile, actOpenProjectFile, actStencilLiveSync, actDeleteProjectFile}));
    addWrappedSeparator(tb);
    connectionsSection = makeToolSection("Connections & chat", {actConnect, actChat});
    addWrapped(tb, connectionsSection);
    addWrappedSeparator(tb);
    // Filter combo + tint swatch are built here, wired in buildStyleToolbar; data carries the
    // canonical value.
    imageFilter = new SearchComboBox(this, /*searchable=*/false);
    imageFilter->addItem("No Filter", "none");
    imageFilter->addItem("B&W", "bw");
    imageFilter->addItem("Sepia", "sepia");
    imageFilter->addItem("Invert", "invert");
    imageFilter->addItem("Contour", "contour");
    imageFilter->addItem("Tint", "custom");
    // The browser's #image-filter tooltip, composed and kept current by tipContent.
    setTipBase(imageFilter, "Image Filter");
    setTipHotkey(imageFilter, actCycleFilter);
    setTipReason(imageFilter, "Load an image to apply a filter");
    filterColorBtn = new QToolButton(this);
    filterColorBtn->setToolTip("Tint color");
    updateColorSwatch(filterColorBtn, filterColorValue);
    filterColorBtn->setVisible(false);   // shown only for the "custom" filter
    addWrapped(tb, makeToolSection("Edit",
                                   {actCrop, actRotateLeft, actRotateRight, actUndo, actRedo},
                                   {nameBar.blankColorBtn}, {imageFilter, filterColorBtn}));

    // One Start/Stop button (refreshActions swaps its default action, like the browser's #draw-
    // toggle); the toggle is wired in buildStyleToolbar.
    drawModeBtn = new QToolButton(this);
    drawModeBtn->setObjectName("drawFaceBtn");   // theme.cpp: the pair's larger word
    drawModeBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    drawModeBtn->setText("Line");
    drawModeBtn->setAutoRaise(true);
    drawModeBtn->setIconSize(QSize(TOOL_ICON + FACE_ICON_GAP, TOOL_ICON));
    setTipBase(drawModeBtn, "Drawing mode: Line (click to switch to Rectangle)");
    setTipReason(drawModeBtn, "Load an image to switch line / rectangle");   // #draw-mode-toggle
    // Solid accent permanently: no QAction for styleDangerToolButtons to reach, and the browser's
    // #draw-mode-toggle is filled at rest too.
    drawModeBtn->setProperty("toolFill", QStringLiteral("accent"));
    // The editable percent combo replaces the browser's +/- steppers; Fit follows it, as in the
    // browser.
    zoomFitBtn = new QToolButton(this);
    zoomFitBtn->setProperty("zoomFit", true);
    zoomFitBtn->setDefaultAction(actFit);
    zoomFitBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    zoomFitBtn->setAutoRaise(true);
    zoomFitBtn->setIconSize(QSize(TOOL_ICON, TOOL_ICON));
    // Draw / Zoom / Settings sit on the rows below: all seven sections need ~1230px, and the
    // browser splits them the same way.
  }

}  // namespace stencil::gui
