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
    nameBar_.blankColorBtn = new QToolButton(this);
    // updateColorSwatch gives it the same 46×26 chip as the line-colour chip; labelled "Blank" as
    // in the browser.
    nameBar_.blankColorBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    nameBar_.blankColorBtn->setText("Blank");
    nameBar_.blankColorBtn->setToolTip("Blank background color — recolor this blank image (keeps your lines)");
    nameBar_.blankColorBtn->setVisible(false);
    connect(nameBar_.blankColorBtn, &QToolButton::clicked, this, [this] { setActiveBlankColor(); });

    // Dialog icons answer dblclick / right-click with the compact popover; a plain click is
    // deferred one double-click interval because exec() blocks.
    pop_.dialogActions = {actOpen_, actOpenAnother_, actOpenIn_, actProjects_, actConnect_, actLinks_,
                             actDescription_, actKeywords_, actChat_, actAssistantSettings_, actShortcuts_,
                             actSettings_, actInfo_};
    pop_.clickTimer = new QTimer(this);
    pop_.clickTimer->setSingleShot(true);
    pop_.clickTimer->setInterval(250);
    connect(pop_.clickTimer, &QTimer::timeout, this, [this] {
      if (QAction* act = pop_.pendingAction.data()) {
        pop_.pendingAction.clear();
        act->trigger();
      }
    });

    // With no image the cluster is one labelled "Open Image" button; refreshActions swaps in the
    // icon row (browser: #load-image-btn ↔ #open-image-btn).
    openImageBtn_ = new OpenImageButton(this);
    openImageBtn_->setDefaultAction(actOpen_);
    openImageBtn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    openImageBtn_->setAutoRaise(true);
    openImageBtn_->setIconSize(QSize(TOOL_ICON, TOOL_ICON));
    {  // 14px label, matching the browser's #load-image-btn (its default button font).
      QFont f = openImageBtn_->font();
      f.setPixelSize(14);
      openImageBtn_->setFont(f);
    }
    // OpenImageButton centres its own icon+label, so no icon-slot reserve to compensate for.
    imageSection_ = makeToolSection("Image",
                                    {actSaveImage_, actCopyImage_, actShareImage_, actOpenIn_, actOpenAnother_},
                                    {}, {openImageBtn_});
    addWrapped(tb, imageSection_);
    addWrappedSeparator(tb);
    // Description, keywords and links all gate on a saved, non-incognito project
    // (updateProjectTitle).
    addWrapped(tb, makeToolSection("Description & attributes", {actDescription_, actKeywords_, actLinks_}));
    addWrappedSeparator(tb);
    addWrapped(tb, makeToolSection("Projects", {actProjects_, actSaveProjectFile_, actOpenProjectFile_, actStencilLiveSync_, actDeleteProjectFile_}));
    addWrappedSeparator(tb);
    connectionsSection_ = makeToolSection("Connections & chat", {actConnect_, actChat_});
    addWrapped(tb, connectionsSection_);
    addWrappedSeparator(tb);
    // Filter combo + tint swatch are built here, wired in buildStyleToolbar; data carries the
    // canonical value.
    imageFilter_ = new SearchComboBox(this, /*searchable=*/false);
    imageFilter_->addItem("No Filter", "none");
    imageFilter_->addItem("B&W", "bw");
    imageFilter_->addItem("Sepia", "sepia");
    imageFilter_->addItem("Invert", "invert");
    imageFilter_->addItem("Contour", "contour");
    imageFilter_->addItem("Tint", "custom");
    // The browser's #image-filter tooltip, composed and kept current by tipContent.
    setTipBase(imageFilter_, "Image Filter");
    setTipHotkey(imageFilter_, actCycleFilter_);
    setTipReason(imageFilter_, "Load an image to apply a filter");
    filterColorBtn_ = new QToolButton(this);
    filterColorBtn_->setToolTip("Tint color");
    updateColorSwatch(filterColorBtn_, filterColorValue_);
    filterColorBtn_->setVisible(false);   // shown only for the "custom" filter
    addWrapped(tb, makeToolSection("Edit",
                                   {actCrop_, actRotateLeft_, actRotateRight_, actUndo_, actRedo_},
                                   {nameBar_.blankColorBtn}, {imageFilter_, filterColorBtn_}));

    // One Start/Stop button (refreshActions swaps its default action, like the browser's #draw-
    // toggle); the toggle is wired in buildStyleToolbar.
    drawModeBtn_ = new QToolButton(this);
    drawModeBtn_->setObjectName("drawFaceBtn");   // theme.cpp: the pair's larger word
    drawModeBtn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    drawModeBtn_->setText("Line");
    drawModeBtn_->setAutoRaise(true);
    drawModeBtn_->setIconSize(QSize(TOOL_ICON + FACE_ICON_GAP, TOOL_ICON));
    setTipBase(drawModeBtn_, "Drawing mode: Line (click to switch to Rectangle)");
    setTipReason(drawModeBtn_, "Load an image to switch line / rectangle");   // #draw-mode-toggle
    // Solid accent permanently: no QAction for styleDangerToolButtons to reach, and the browser's
    // #draw-mode-toggle is filled at rest too.
    drawModeBtn_->setProperty("toolFill", QStringLiteral("accent"));
    // The editable percent combo replaces the browser's +/- steppers; Fit follows it, as in the
    // browser.
    zoomFitBtn_ = new QToolButton(this);
    zoomFitBtn_->setProperty("zoomFit", true);
    zoomFitBtn_->setDefaultAction(actFit_);
    zoomFitBtn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    zoomFitBtn_->setAutoRaise(true);
    zoomFitBtn_->setIconSize(QSize(TOOL_ICON, TOOL_ICON));
    // Draw / Zoom / Settings sit on the rows below: all seven sections need ~1230px, and the
    // browser splits them the same way.
  }

}  // namespace stencil::gui
