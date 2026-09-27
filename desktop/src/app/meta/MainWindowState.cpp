#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "RemoteSyncController.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "StencilFileSync.hpp"
#include "tipContent.hpp"

#include <QTimer>
#include <QAction>
#include <QLayout>

// The action and selection syncs that follow a canvas change, the script flyout's way back to its
// row, and an action's rich tooltip, composed the way every toolbar and menu tip is.

namespace stencil::gui {

  void MainWindow::onCanvasChanged() {
    refreshActions();
    onSelectionChanged();
    scheduleAutosave();
    remoteSync->scheduleRemotePush();   // live co-edit: push the edit to the server for peers
    stencilSync->scheduleAutosave();     // live file sync: auto-save the edit to the linked .stencil
  }

  // Live co-edit push/pull lives in RemoteSyncController (remoteSync); the remote-link state and
  // reentrancy flags stay here.

  void MainWindow::onSelectionChanged() {
    // No image → no points panel, or restored lines float over the empty canvas.
    const bool hasImg = canvas->hasImage();
    const core::Line* line = hasImg ? canvas->panelLine() : nullptr;
    // Same pageCoords converter as the status bar and tooltip; mirrors
    // browser/js/ui/panel/coordTable.js.
    const auto u = unitFormat();
    // The unit rides in the column headings, not every cell (browser: `X cm` / `Y cm`), and tracks
    // the setting with no line on screen.
    selPanel->setUnitLabel(QString::fromStdString(u.label));
    std::vector<SelectionPanel::PageRow> pageRows;
    if (line && canvas->hasImage()) {
      pageRows.reserve(line->points.size());
      for (const auto& p : line->points) {
        const auto page = pageCoords(p.x, p.y);
        pageRows.push_back({QString::number(page.x * u.factor, 'f', 2),
                            QString::number(page.y * u.factor, 'f', 2)});
      }
    }
    selPanel->showLine(line, hasImg ? canvas->getSelectedPoint() : -1, pageRows);
    // Dust plays only on the hidden<->visible edge (browser selectionPanel.js wasHidden), never on
    // repopulating.
    const core::Line* editorLine = hasImg ? canvas->selectedLine() : nullptr;
    const bool wasBarVisible = selectedLineDock->isVisible();
    const bool showBar = editorLine != nullptr;
    selectedLineBar->showLine(editorLine);
    // The Image Size dock's top margin drops to 0 while the bar is up, or the browser's gap
    // doubles.
    if (tools.imageInfoHost)
      if (auto* hostLay = tools.imageInfoHost->layout()) {
        QMargins m = hostLay->contentsMargins();
        m.setTop(showBar ? 0 : 8);
        hostLay->setContentsMargins(m);
      }
    if (showBar && !wasBarVisible) {
      selectedLineDock->setVisible(true);
      // splitDockWidget against a hidden dock does not register, so re-affirm the stack once it is
      // on screen. Idempotent.
      if (tools.imageInfoDock) editor->splitDockWidget(selectedLineDock, tools.imageInfoDock, Qt::Vertical);
      if (QLayout* l = editor->layout()) l->activate();   // the grab must see the shown bar, not a stale one
      parts.dockChrome.dustSelectedLineBarIn();
    } else if (!showBar && wasBarVisible) {
      parts.dockChrome.dustSelectedLineBarOut();
      selectedLineDock->setVisible(false);
    } else {
      selectedLineDock->setVisible(showBar);
    }
    selPanel->setMultiSelectCount(hasImg ? canvas->selectionCount() : 0);
    if (hasImg) selPanel->setLines(canvas->getLines(), canvas->selectedIndices());
    else selPanel->setLines({}, {});
  }

  // The chain the file dialog took down, back where it was and on the script row. Queued, so
  // the picker's own modal loop is fully unwound before the menu's begins.
  void MainWindow::reopenScriptFlyout() {
    if (ctxMenu.menuAt.isNull() || !canvas || !canvas->hasImage()) return;
    ctxMenu.reopenScriptPending = true;
    const QPoint at = ctxMenu.menuAt;
    QTimer::singleShot(0, this, [this, at] { parts.canvasMenu.showContextMenu(at); });
  }

  void MainWindow::setActionTip(QAction* a, const QString& desc) {
    // tipContent keeps "desc (shortcut)" + the disabled reason composed (browser composeControlTitle).
    setTipBase(a, desc);
  }

}  // namespace stencil::gui
