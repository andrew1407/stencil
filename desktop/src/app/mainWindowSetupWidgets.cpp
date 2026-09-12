// MainWindow construction, phase 3 of 4: the hover shimmer over the panels' own controls,
// the toast stack and canvas overlays, and the page-format + zoom combos. Order is pinned;
// see mainWindowSetupCanvas.cpp.
#include "mainWindow.hpp"
#include "canvasWidget.hpp"
#include "chatDock.hpp"
#include "selectionPanel.hpp"
#include "selectedLineBar.hpp"
#include "canvasTooltip.hpp"
#include "dataExportController.hpp"
#include "dropZonesOverlay.hpp"
#include "incognitoOverlay.hpp"
#include "guiHelpers.hpp"   // fillPageSizeCombo
#include "mainWindowShared.hpp"
#include "notifications.hpp"
#include "projectDragZones.hpp"
#include "remoteSession.hpp"
#include "searchCombo.hpp"
#include "../support/menuShimmer.hpp"
#include "../support/tipContent.hpp"
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>

namespace stencil::gui {

  void MainWindow::installPanelShimmers() {
    // The same hover shimmer the selection panel's buttons get.
    for (QAbstractButton* b : chatDock_->findChildren<QAbstractButton*>())
      installHoverShimmer(b);
    // Shared hover shimmer for the right Points/Lines panel: per-button on its buttons, and
    // per-ROW on its points table + lines list (item-view rows aren't widgets, so the overlay
    // tracks the hovered row) — matching the browser's coord-panel shimmer.
    for (QAbstractButton* b : selPanel_->findChildren<QAbstractButton*>()) installHoverShimmer(b);
    for (QAbstractItemView* v : selPanel_->findChildren<QAbstractItemView*>()) installRowShimmer(v);
    // The "Selected Line:" bar's own controls — browser parity: EVERY <button> (and,
    // per mainWindowToolbar's toolbar sweep, combo/spin controls too) gets the shimmer,
    // the swatches and Deselect included.
    for (QAbstractButton* b : selectedLineBar_->findChildren<QAbstractButton*>())
      installHoverShimmer(b);
    for (QComboBox* c : selectedLineBar_->findChildren<QComboBox*>()) installHoverShimmer(c);
    for (QAbstractSpinBox* s : selectedLineBar_->findChildren<QAbstractSpinBox*>())
      installHoverShimmer(s);
  }

  void MainWindow::setupOverlaysAndStatus() {
    // Parented to the WINDOW, not the canvas viewport: the clear/paste effects
    // (DisintegrateOverlay) raise() themselves over that viewport, and a toast parented
    // there ended up painted UNDER the scattering motes. It also matches the browser,
    // whose #notify-balloon is position:fixed to the window rather than to the canvas.
    notify_ = new Notifications(this);
    // Server-project session domain (remoteSession.hpp): owns the remote-link state + the
    // ConnectionManager handle + the version-guarded write helpers. Created before the sync
    // controller (which composes it). Its ConnectionManager is set in ensureConnections().
    remoteSession_ = new RemoteSession(this, notify_);
    // Layout/image export + clipboard IO (dataExportController.hpp). Needs canvas_ + notify_ +
    // settings_, plus the project name + layout-meta accessors that stay on MainWindow.
    dataExport_ = std::make_unique<DataExportController>(
        this, canvas_, notify_, &settings_,
        [this] { return projectBaseName(); },
        [this] { return currentLayoutMeta(); });
    // Incognito indicator (dashed frame + badge) pinned to the canvas viewport,
    // mirroring the browser's body.incognito-mode outline/badge. Hidden until the
    // incognito action toggles it on.
    incognitoOverlay_ = new IncognitoOverlay(scroll_->viewport());
    // Split image-drop overlay (LEFT save / RIGHT incognito), shown while dragging a file.
    dropZones_ = new DropZonesOverlay(scroll_->viewport());
    // Accent lands in the theme apply below (QPalette::Highlight here was the OS
    // selection blue, not the app accent).
    // 3-zone overlay shown behind the Projects dialog while a project row is dragged out of it.
    projectZones_ = new ProjectDragZones(scroll_->viewport());
    tooltip_ = new CanvasTooltip(this);

    // Live cursor coord readout (Pixel/Page/To edge) — browser parity: #coord-status sits in
    // the central-layout row between .canvas-viewport and .drop-hint, not at the bottom of
    // the page. Empty while the cursor is off the canvas (no "Ready" filler, matching the
    // browser) and hidden during fullscreen (see toggleFullscreen).
    status_ = new QLabel(QString(), centralWidget());   // cursor readout only — blank until one hovers the canvas
    status_->setObjectName("coordStatus");
    status_->setAttribute(Qt::WA_StyledBackground, true);
    status_->setStyleSheet("font-family: monospace;");
    centralLayout_->insertWidget(centralLayout_->indexOf(dropHint_), status_);
  }

  void MainWindow::setupPageAndZoomControls() {
    // Custom page + the full ISO 216/269 A/B/C series. Items carry the
    // canonical value ("custom"/"A4") as DATA (read via pageSizeValue()); labels
    // add the physical size in the active display unit and are re-rendered by
    // applyUnitToPageCombo() when the unit changes. SearchComboBox opens the
    // browser-style themed popup with the pinned "Search…" filter (the port of
    // enhanceSelect({ search: true }) on #page-size) — the trigger itself stays
    // a plain, non-editable combo.
    units_.pageSize = new SearchComboBox(this);
    fillPageSizeCombo(units_.pageSize, /*includeCustom=*/true);
    units_.pageSize->setToolTip("Page size");
    // The CLOSED combo is sized by the longest entry ("B0 (100 × 141.4 cm)"), which
    // made this the widest control on the row and pushed the SETTINGS cluster past
    // the window edge on an ordinary laptop screen. The dimensions are a reminder,
    // not the label — the popup (and the tooltip) still show them in full.
    units_.pageSize->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    units_.pageSize->setMinimumContentsLength(11);
    units_.pageSize->setMaximumWidth(150);
    zoom_ = new QComboBox(this);
    zoom_->addItems({"10%", "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%", "500%", "800%", "1600%", "3200%"});
    setTipBase(zoom_, "Zoom %");   // browser #zoom-input: greyed with nothing to zoom
    setTipReason(zoom_, "Load an image to zoom");
    zoom_->setMaximumWidth(88);   // "3200%" plus the arrow; the rest was slack
    // Editable so the user can type an exact percent, but NoInsert so reflecting
    // a programmatic zoom (Ctrl+wheel) never appends list items — mirrors browser
    // zoomPan.js setZoom (a clamped numeric percent, never an accumulating list).
    zoom_->setEditable(true);
    zoom_->setInsertPolicy(QComboBox::NoInsert);
    zoom_->setCurrentText("100%");
    // Open the preset list as soon as the field is focused (click/tab), so a single control
    // offers BOTH typing and preset-picking without a separate dropdown gesture — the popup
    // still lets the user keep typing. Guarded by focus reason so it doesn't reopen when focus
    // returns from the just-closed popup (which would loop).
    zoom_->lineEdit()->installEventFilter(this);
  }

}  // namespace stencil::gui
