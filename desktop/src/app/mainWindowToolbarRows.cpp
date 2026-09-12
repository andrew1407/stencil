// buildMainToolbar()'s second row: the one wrapping tool row of named, stacked sections, in the
// browser topbar order — Image · Projects · Share · Edit. Everything here collapses with the
// Controls pill; the header row above it does not.
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasWidget.hpp"
#include "iconSet.hpp"
#include "logoHoverFx.hpp"
#include "controlsPill.hpp"
#include "searchCombo.hpp"
#include "openImageButton.hpp"
#include "theme.hpp"
#include "../support/controlReveal.hpp"
#include "../support/iconMotion.hpp"
#include "../support/shimmerOverlay.hpp"
#include "../support/wrapRow.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace stencil::gui {

  // Everything that collapses: one wrapping row of named, stacked sections in the browser
  // topbar order — Image · Projects · Share · Edit.
  void MainWindow::buildToolSectionsRow() {

    // The one tool row: it wraps, so nothing reaches QToolBar's "»" (see buildToolbar).
    auto* tb = addToolBar("Main");
    tb->setObjectName("mainToolbar");  // named for QMainWindow::saveState
    tb->setMovable(false);
    // Icon-only with the shared line-art glyphs (styleActionIcons assigns them) +
    // the rich tooltips from mk(): compact, browser-faithful chrome that stays
    // narrow enough to avoid the "»" overflow even with the full action set.
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->setIconSize(QSize(kToolIcon, kToolIcon));

    // Named, stacked groups (label ABOVE the icon row via makeToolSection) mirror the browser
    // topbar order: Image · Projects · Share · Edit · Draw · Zoom · Settings.
    // Blank-background swatch (browser parity): a colour button shown only for blank projects,
    // recolouring the fill (lines kept). Lives in the IMAGE group; gated in updateProjectTitle.
    nameBar_.blankColorBtn = new QToolButton(this);
    // Visuals come entirely from updateColorSwatch (the shared 46×26 chip
    // recipe), so it presents the SAME control height as the line-colour chip.
    // Labelled "Blank" beside the chip, as in the browser: a bare white swatch in the middle
    // of the Edit group says nothing about what it recolours.
    nameBar_.blankColorBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    nameBar_.blankColorBtn->setText("Blank");
    nameBar_.blankColorBtn->setToolTip("Blank background color — recolor this blank image (keeps your lines)");
    nameBar_.blankColorBtn->setVisible(false);
    connect(nameBar_.blankColorBtn, &QToolButton::clicked, this, [this] { setActiveBlankColor(); });

    // The dialog-opening icons answer dblclick / right-click with the COMPACT anchored
    // shape of their dialog (execMaybePopover). A plain click keeps the full dialog, but
    // deferred one double-click interval (the logo pattern): the dialog's exec() blocks,
    // so an instant open would swallow the second click of every double-click.
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

    // Image cluster (browser parity) + blank-fill swatch. Empty state: with no
    // image the cluster collapses to ONE labelled "Open Image" button; once an
    // image loads, refreshActions swaps it for the icon row, whose trailing icon
    // (actOpenAnother_) opens the same dialog as this button's actOpen_ — browser
    // parity: #load-image-btn ↔ #open-image-btn, same handler, different button/icon.
    openImageBtn_ = new OpenImageButton(this);
    openImageBtn_->setDefaultAction(actOpen_);
    openImageBtn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    openImageBtn_->setAutoRaise(true);
    openImageBtn_->setIconSize(QSize(kToolIcon, kToolIcon));
    {  // 14px label, matching the browser's #load-image-btn (its default button font).
      QFont f = openImageBtn_->font();
      f.setPixelSize(14);
      openImageBtn_->setFont(f);
    }
    // OpenImageButton paints its own icon+label as one centred group with a wide gap
    // between them, so there is no stock icon-slot reserve to compensate for.
    // Guarded by openImageButtonLabelIsCentred() in the GUI tests.
    imageSection_ = makeToolSection("Image",
                                    {actSaveImage_, actCopyImage_, actShareImage_, actOpenIn_, actOpenAnother_},
                                    {}, {openImageBtn_});
    addWrapped(tb, imageSection_);
    addWrappedSeparator(tb);
    // Description & attributes: the saved project's description, keywords and links —
    // the browser's cluster between IMAGE and PROJECTS. All three gate on a saved,
    // non-incognito project (updateProjectTitle), so the whole group reads as one rule.
    addWrapped(tb, makeToolSection("Description & attributes", {actDescription_, actKeywords_, actLinks_}));
    addWrappedSeparator(tb);
    // Projects = open editor list + save/open .stencil + live-sync, matching the browser's
    // PROJECTS cluster (layers / save / folder / refresh). Clear-project stays in the menu bar.
    addWrapped(tb, makeToolSection("Projects", {actProjects_, actSaveProjectFile_, actOpenProjectFile_, actStencilLiveSync_, actDeleteProjectFile_}));
    addWrappedSeparator(tb);
    // Connections & chat: servers (connect to share/co-edit) + the AI-assistant sparkle
    // toggle (identical grouping to the browser toolbar; the image's source links live in
    // DESCRIPTION & ATTRIBUTES above, as in the browser).
    connectionsSection_ = makeToolSection("Connections & chat", {actConnect_, actChat_});
    addWrapped(tb, connectionsSection_);
    addWrappedSeparator(tb);
    // Edit cluster (browser parity; blank-recolour chip closes it). The filter
    // combo + tint swatch open the group — built here, WIRED in buildStyleToolbar
    // (which runs next). data carries the canonical value.
    imageFilter_ = new SearchComboBox(this, /*searchable=*/false);
    imageFilter_->addItem("No Filter", "none");
    imageFilter_->addItem("B&W", "bw");
    imageFilter_->addItem("Sepia", "sepia");
    imageFilter_->addItem("Invert", "invert");
    imageFilter_->addItem("Contour", "contour");
    imageFilter_->addItem("Tint", "custom");
    // The browser's #image-filter: heading, Cycle Image Filter's chord as its keycap, and
    // the reason it is greyed out (composed and kept current by tipContent).
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

    // Draw = ONE Start/Stop button (refreshActions swaps its default action, like
    // the browser's single #draw-toggle) + the Line/Rect mode toggle. The toggle
    // is built here, wired in buildStyleToolbar (which needs the canvas signals).
    drawModeBtn_ = new QToolButton(this);
    // Icon + label (the glyph is themed in styleActionIcons / the drawModeChanged handler).
    drawModeBtn_->setObjectName("drawFaceBtn");   // theme.cpp: the pair's larger word
    drawModeBtn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    drawModeBtn_->setText("Line");
    drawModeBtn_->setAutoRaise(true);
    drawModeBtn_->setIconSize(QSize(kToolIcon + kFaceIconGap, kToolIcon));
    setTipBase(drawModeBtn_, "Drawing mode: Line (click to switch to Rectangle)");
    setTipReason(drawModeBtn_, "Load an image to switch line / rectangle");   // #draw-mode-toggle
    // Solid accent, permanently — it has no QAction for styleDangerToolButtons' own
    // fill pass to reach (its click is a plain connect(), not a default action), and
    // the browser's `#draw-mode-toggle` is a bare `<button>`, filled at rest too.
    drawModeBtn_->setProperty("toolFill", QStringLiteral("accent"));
    // Zoom = the editable percent combo + a Fit-to-window button (browser parity — the browser's
    // zoom section ends with the fit icon). The combo replaces the browser's +/- steppers (type or
    // pick a preset). Fit button built here so it sits AFTER the combo, like the browser.
    zoomFitBtn_ = new QToolButton(this);
    zoomFitBtn_->setDefaultAction(actFit_);
    // Filled like the other acting buttons (browser #zoom-fit); the property is kept for
    // its DISABLED face alone — a faded outline rather than a filled chip, since it ends
    // the ZOOM row beside a plain field (theme.cpp QToolButton[toolGhost="true"]:disabled).
    zoomFitBtn_->setProperty("toolGhost", true);
    zoomFitBtn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    zoomFitBtn_->setAutoRaise(true);
    zoomFitBtn_->setIconSize(QSize(kToolIcon, kToolIcon));
    // Draw / Zoom / Settings are NOT on this row: all seven sections together need
    // ~1230px, so on a 1000px window Zoom and Settings (incognito!) were pushed clean
    // off the end with no way to reach them. They now sit on the rows below, which is
    // also exactly how the browser splits them — row 1 is Image · Projects ·
    // Connections & chat · Edit there too.
  }

}  // namespace stencil::gui
