// MainWindow construction, phase 3 of 4: panel shimmer, toasts, overlays, the page-format + zoom
// combos. Order is pinned; see mainWindowSetupCanvas.cpp.
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
    for (QAbstractButton* b : chatDock_->findChildren<QAbstractButton*>())
      installHoverShimmer(b);
    // Per-row on the points table + lines list (item-view rows are not widgets), matching the
    // browser's coord-panel shimmer.
    for (QAbstractButton* b : selPanel_->findChildren<QAbstractButton*>()) installHoverShimmer(b);
    for (QAbstractItemView* v : selPanel_->findChildren<QAbstractItemView*>()) installRowShimmer(v);
    for (QAbstractButton* b : selectedLineBar_->findChildren<QAbstractButton*>())
      installHoverShimmer(b);
    for (QComboBox* c : selectedLineBar_->findChildren<QComboBox*>()) installHoverShimmer(c);
    for (QAbstractSpinBox* s : selectedLineBar_->findChildren<QAbstractSpinBox*>())
      installHoverShimmer(s);
  }

  void MainWindow::setupOverlaysAndStatus() {
    // Parented to the window, not the viewport: DisintegrateOverlay raises itself over the
    // viewport and painted over a toast there (browser: #notify-balloon is position:fixed).
    notify_ = new Notifications(this);
    // Created before the sync controller, which composes it; its ConnectionManager is set in
    // ensureConnections().
    remoteSession_ = new RemoteSession(this, notify_);
    dataExport_ = std::make_unique<DataExportController>(
        this, canvas_, notify_, &settings_,
        [this] { return projectBaseName(); },
        [this] { return currentLayoutMeta(); });
    // Mirrors the browser's body.incognito-mode outline/badge.
    incognitoOverlay_ = new IncognitoOverlay(scroll_->viewport());
    dropZones_ = new DropZonesOverlay(scroll_->viewport());
    // Accent lands in the theme apply below (QPalette::Highlight is the OS selection blue).
    projectZones_ = new ProjectDragZones(scroll_->viewport());
    tooltip_ = new CanvasTooltip(this);

    // browser #coord-status sits between .canvas-viewport and .drop-hint; empty off-canvas, hidden
    // in fullscreen.
    status_ = new QLabel(QString(), centralWidget());   // cursor readout only — blank until one hovers the canvas
    status_->setObjectName("coordStatus");
    status_->setAttribute(Qt::WA_StyledBackground, true);
    status_->setStyleSheet("font-family: monospace;");
    centralLayout_->insertWidget(centralLayout_->indexOf(dropHint_), status_);
  }

  void MainWindow::setupPageAndZoomControls() {
    // Items carry the canonical value as data (pageSizeValue()); labels are re-rendered by
    // applyUnitToPageCombo(). SearchComboBox is the port of enhanceSelect({ search: true }).
    units_.pageSize = new SearchComboBox(this);
    fillPageSizeCombo(units_.pageSize, /*includeCustom=*/true);
    units_.pageSize->setToolTip("Page size");
    // The closed combo is sized by the longest entry ("B0 (100 × 141.4 cm)") and pushed SETTINGS
    // off a laptop screen; the popup still shows dimensions.
    units_.pageSize->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    units_.pageSize->setMinimumContentsLength(11);
    units_.pageSize->setMaximumWidth(150);
    zoom_ = new QComboBox(this);
    zoom_->addItems({"10%", "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%", "500%", "800%", "1600%", "3200%"});
    setTipBase(zoom_, "Zoom %");   // browser #zoom-input: greyed with nothing to zoom
    setTipReason(zoom_, "Load an image to zoom");
    zoom_->setMaximumWidth(88);   // "3200%" plus the arrow; the rest was slack
    // NoInsert so reflecting a programmatic zoom never appends list items (browser zoomPan.js
    // setZoom).
    zoom_->setEditable(true);
    zoom_->setInsertPolicy(QComboBox::NoInsert);
    zoom_->setCurrentText("100%");
    // Open the presets on focus so one control offers typing and picking; guarded by focus reason
    // or it reopens from the just-closed popup and loops.
    zoom_->lineEdit()->installEventFilter(this);
  }

}  // namespace stencil::gui
