// MainWindow construction, phase 3 of 4: panel shimmer, toasts, overlays, the page-format + zoom
// combos. Order is pinned; see MainWindowSetupCanvas.cpp.
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "ChatDock.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "CanvasTooltip.hpp"
#include "DataExportController.hpp"
#include "DropZonesOverlay.hpp"
#include "IncognitoOverlay.hpp"
#include "guiHelpers.hpp"   // fillPageSizeCombo
#include "mainWindowShared.hpp"
#include "Notifications.hpp"
#include "ProjectDragZones.hpp"
#include "RemoteSession.hpp"
#include "SearchCombo.hpp"
#include "../../support/motion/MenuShimmer.hpp"
#include "../../support/tip/tipContent.hpp"
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>

namespace stencil::gui {

  void MainWindow::installPanelShimmers() {
    for (QAbstractButton* b : chatDock->findChildren<QAbstractButton*>())
      installHoverShimmer(b);
    // Per-row on the points table + lines list (item-view rows are not widgets), matching the
    // browser's coord-panel shimmer.
    for (QAbstractButton* b : selPanel->findChildren<QAbstractButton*>()) installHoverShimmer(b);
    for (QAbstractItemView* v : selPanel->findChildren<QAbstractItemView*>()) installRowShimmer(v);
    for (QAbstractButton* b : selectedLineBar->findChildren<QAbstractButton*>())
      installHoverShimmer(b);
    for (QComboBox* c : selectedLineBar->findChildren<QComboBox*>()) installHoverShimmer(c);
    for (QAbstractSpinBox* s : selectedLineBar->findChildren<QAbstractSpinBox*>())
      installHoverShimmer(s);
  }

  void MainWindow::setupOverlaysAndStatus() {
    // Parented to the window, not the viewport: DisintegrateOverlay raises itself over the
    // viewport and painted over a toast there (browser: #notify-balloon is position:fixed).
    notify = new Notifications(this);
    // Created before the sync controller, which composes it; its ConnectionManager is set in
    // ensureConnections().
    remoteSession = new RemoteSession(this, notify);
    dataExport = std::make_unique<DataExportController>(
        this, canvas, notify, &settings,
        [this] { return projectBaseName(); },
        [this] { return currentLayoutMeta(); });
    // Mirrors the browser's body.incognito-mode outline/badge.
    incognitoOverlay = new IncognitoOverlay(scroll->viewport());
    dropZones = new DropZonesOverlay(scroll->viewport());
    // Accent lands in the theme apply below (QPalette::Highlight is the OS selection blue).
    projectZones = new ProjectDragZones(scroll->viewport());
    tooltip = new CanvasTooltip(this);

    // browser #coord-status sits between .canvas-viewport and .drop-hint; empty off-canvas, hidden
    // in fullscreen.
    status = new QLabel(QString(), centralWidget());   // cursor readout only — blank until one hovers the canvas
    status->setObjectName("coordStatus");
    status->setAttribute(Qt::WA_StyledBackground, true);
    status->setStyleSheet("font-family: monospace;");
    centralLayout->insertWidget(centralLayout->indexOf(dropHint), status);
  }

  void MainWindow::setupPageAndZoomControls() {
    // Items carry the canonical value as data (pageSizeValue()); labels are re-rendered by
    // applyUnitToPageCombo(). SearchComboBox is the port of enhanceSelect({ search: true }).
    units.pageSize = new SearchComboBox(this);
    fillPageSizeCombo(units.pageSize, /*includeCustom=*/true);
    units.pageSize->setToolTip("Page size");
    // The closed combo is sized by the longest entry ("B0 (100 × 141.4 cm)") and pushed SETTINGS
    // off a laptop screen; the popup still shows dimensions.
    units.pageSize->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    units.pageSize->setMinimumContentsLength(11);
    units.pageSize->setMaximumWidth(150);
    zoom = new QComboBox(this);
    zoom->addItems({"10%", "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%", "500%", "800%", "1600%", "3200%"});
    setTipBase(zoom, "Zoom %");   // browser #zoom-input: greyed with nothing to zoom
    setTipReason(zoom, "Load an image to zoom");
    zoom->setMaximumWidth(88);   // "3200%" plus the arrow; the rest was slack
    // NoInsert so reflecting a programmatic zoom never appends list items (browser zoomPan.js
    // setZoom).
    zoom->setEditable(true);
    zoom->setInsertPolicy(QComboBox::NoInsert);
    zoom->setCurrentText("100%");
    // Open the presets on focus so one control offers typing and picking; guarded by focus reason
    // or it reopens from the just-closed popup and loops.
    zoom->lineEdit()->installEventFilter(this);
  }

}  // namespace stencil::gui
