// MainWindow construction, phase 3 of 4: panel shimmer, toasts, overlays, the page-format + zoom
// combos. Order is pinned; see WindowAssembly.cpp.
#include "MainWindow.hpp"
#include "WindowAssembly.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "CanvasTooltip.hpp"
#include "DataExportController.hpp"
#include "DropZonesOverlay.hpp"
#include "IncognitoOverlay.hpp"
#include "guiHelpers.hpp"   // fillPageSizeCombo
#include "Notifications.hpp"
#include "ProjectDragZones.hpp"
#include "RemoteSession.hpp"
#include "SearchCombo.hpp"
#include "ShimmerOverlay.hpp"
#include "ChatDock.hpp"
#include "tipContent.hpp"
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QLineEdit>

namespace stencil::gui {

  void WindowAssembly::installPanelShimmers() {
    for (QAbstractButton* b : w.chatDock->findChildren<QAbstractButton*>())
      installHoverShimmer(b);
    // Per-row on the points table + lines list (item-view rows are not widgets), matching the
    // browser's coord-panel shimmer.
    for (QAbstractButton* b : w.selPanel->findChildren<QAbstractButton*>()) installHoverShimmer(b);
    for (QAbstractItemView* v : w.selPanel->findChildren<QAbstractItemView*>()) installRowShimmer(v);
    for (QAbstractButton* b : w.selectedLineBar->findChildren<QAbstractButton*>())
      installHoverShimmer(b);
    for (QComboBox* c : w.selectedLineBar->findChildren<QComboBox*>()) installHoverShimmer(c);
    for (QAbstractSpinBox* s : w.selectedLineBar->findChildren<QAbstractSpinBox*>())
      installHoverShimmer(s);
  }

  void WindowAssembly::setupOverlaysAndStatus() {
    // Parented to the window, not the viewport: DisintegrateOverlay raises itself over the
    // viewport and painted over a toast there (browser: #notify-balloon is position:fixed).
    w.notify = new Notifications(&w);
    // Created before the sync controller, which composes it; its ConnectionManager is set in
    // ensureConnections().
    w.remote.session = new RemoteSession(&w, w.notify);
    w.dataExport = std::make_unique<DataExportController>(
        &w, w.canvas, w.notify, &w.settings,
        [this] { return w.projectBaseName(); },
        [this] { return w.currentLayoutMeta(); });
    // Mirrors the browser's body.incognito-mode outline/badge.
    w.overlays.incognito = new IncognitoOverlay(w.scroll->viewport());
    // Hosted by the WINDOW, not the viewport: the browser's #global-drop-overlay covers the
    // whole page, so no dock can move the painted split off the window midline.
    w.overlays.dropZones = new DropZonesOverlay(&w);
    // Accent lands in the theme apply below (QPalette::Highlight is the OS selection blue).
    // The projects list's drag-out zones cover the page as well (browser .project-dropzones).
    w.overlays.projectZones = new ProjectDragZones(&w);
    w.overlays.tooltip = new CanvasTooltip(&w);

    // browser #coord-status sits between .canvas-viewport and .drop-hint; empty off-canvas, hidden
    // in fullscreen.
    w.status = new QLabel(QString(), w.editor->centralWidget());   // cursor readout only — blank until one hovers the canvas
    w.status->setObjectName("coordStatus");
    w.status->setAttribute(Qt::WA_StyledBackground, true);
    w.status->setStyleSheet("font-family: monospace;");
    w.centralLayout->insertWidget(w.centralLayout->indexOf(w.tools.dropHint), w.status);
  }

  void WindowAssembly::setupPageAndZoomControls() {
    // Items carry the canonical value as data (pageSizeValue()); labels are re-rendered by
    // applyUnitToPageCombo(). SearchComboBox is the port of enhanceSelect({ search: true }).
    w.units.pageSize = new SearchComboBox(&w);
    fillPageSizeCombo(w.units.pageSize, /*includeCustom=*/true);
    w.units.pageSize->setToolTip("Page size");
    // The closed combo is sized by the longest entry ("B0 (100 × 141.4 cm)") and pushed SETTINGS
    // off a laptop screen; the popup still shows dimensions.
    w.units.pageSize->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    w.units.pageSize->setMinimumContentsLength(11);
    w.units.pageSize->setMaximumWidth(150);
    // The themed preset list every other selector opens (browser: the zoom menu is an .accent-dd).
    w.zoom = new SearchComboBox(&w, /*searchable=*/false);
    w.zoom->setObjectName(QStringLiteral("zoomCombo"));
    w.zoom->addItems({"10%", "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%", "500%", "800%", "1600%", "3200%"});
    setTipBase(w.zoom, "Zoom %");   // browser #zoom-input: greyed with nothing to zoom
    setTipReason(w.zoom, "Load an image to zoom");
    w.zoom->setMaximumWidth(88);   // "3200%" plus the arrow; the rest was slack
    // NoInsert so reflecting a programmatic zoom never appends list items (browser zoom/pan.js
    // setZoom).
    w.zoom->setEditable(true);
    w.zoom->setInsertPolicy(QComboBox::NoInsert);
    w.zoom->setCurrentText("100%");
    // Open the presets on focus so one control offers typing and picking; guarded by focus reason
    // or it reopens from the just-closed popup and loops.
    w.zoom->lineEdit()->installEventFilter(&w);
  }

}  // namespace stencil::gui
