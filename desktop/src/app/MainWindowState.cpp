#include "MainWindow.hpp"
#include <QScrollArea>
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "OpenInDialog.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "numericInput.hpp"
#include "InfoDialog.hpp"
#include "LinksDialog.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "DataExportController.hpp"
#include "RemoteSyncController.hpp"
#include "ServerClient.hpp"
#include "SelectionPanel.hpp"
#include "SelectedLineBar.hpp"
#include "SettingsDialog.hpp"
#include "ShortcutsDialog.hpp"
#include "../support/controlSwap.hpp"
#include "../support/modalChrome.hpp"
#include "../support/iconMotion.hpp"

#include <QAction>
#include <QKeySequence>
#include <QLayout>

// Formula commit plus the action/selection state syncs that follow a canvas change.

namespace stencil::gui {

  // The browser's wireFormulaInputs commit: on settle, Enter, focus-out or a programmatic set —
  // never per keystroke.
  void MainWindow::validateAndApplyFormulas() {
    // A focus-out commit also arrives during destruction, when the controllers are gone. Same
    // guard as the chat dock.
    if (tearingDown_) return;
    if (formulaCommitTimer_) formulaCommitTimer_->stop();   // a direct call pre-empts the pause
    const QString fx = formulaX_->text().trimmed();
    const QString fy = formulaY_->text().trimmed();
    const bool okX = core::FormulaParser::validate(fx.toStdString(), 'x');
    const bool okY = core::FormulaParser::validate(fy.toStdString(), 'y');
    formulaError_->setVisible(!okX || !okY);
    if (okX && okY) {
      settings_.formulaX = fx;
      settings_.formulaY = fy;
      persistSettings();
      onHovered(lastHoverX_, lastHoverY_);
      onSelectionChanged();  // refresh panel cm with the new formulas (GAP-2)
      remoteSync_->scheduleRemotePush();  // formulas ride the layout — push to peers
    }
  }

  // Two enabled actions cannot share a shortcut, so Ctrl+C/Ctrl+Shift+D moves onto the split
  // action while comparing.
  void MainWindow::syncSplitCopyDownloadSlot() {
    const bool split = canvas_->isSplitCompare();
    actCopyImageSplit_->setVisible(split);
    actSaveImageSplit_->setVisible(split);
    // Idempotent: a moved shortcut leaves the source empty.
    auto moveShortcut = [](QAction* from, QAction* to) {
      if (!from->shortcut().isEmpty()) { to->setShortcut(from->shortcut()); from->setShortcut(QKeySequence()); }
    };
    if (split) {
      moveShortcut(actCopyImage_, actCopyImageSplit_);
      moveShortcut(actSaveImage_, actSaveImageSplit_);
    } else {
      moveShortcut(actCopyImageSplit_, actCopyImage_);
      moveShortcut(actSaveImageSplit_, actSaveImage_);
    }
    // "Current"'s own row carries a manual "\t"+combo hint mirroring the live primary combo
    // (browser: ctx-copy-img-current-hk swap).
    auto setRowHint = [](QAction* row, QAction* primary) {
      const QString combo = primary->shortcut().isEmpty()
          ? QString() : primary->shortcut().toString(QKeySequence::NativeText);
      row->setText(QStringLiteral("Current (Tint + Lines/Points)") +
                   (combo.isEmpty() ? QString() : QStringLiteral("\t") + combo));
    };
    setRowHint(actCopyImageCurrentRow_, actCopyImage_);
    setRowHint(actSaveImageCurrentRow_, actSaveImage_);
  }

  // Browser contextMenu.js syncState parity: rows that would render byte-identical to a sibling
  // hide.
  void MainWindow::syncExportActions() {
    const bool hasImg = canvas_->hasImage();
    const bool hasLines = !canvas_->allLines().empty();
    actCopyImage_->setEnabled(hasImg);
    actSaveImage_->setEnabled(hasImg);
    actCopyImageSplit_->setEnabled(hasImg);
    actSaveImageSplit_->setEnabled(hasImg);
    actCopyImageCurrentRow_->setEnabled(hasImg);
    actSaveImageCurrentRow_->setEnabled(hasImg);
    actCopyImageOriginal_->setEnabled(hasImg);
    actCopyImageTint_->setEnabled(hasImg);
    actSaveImageOriginal_->setEnabled(hasImg);
    actSaveImageTint_->setEnabled(hasImg);
    const bool hasFilter = settings_.imageFilter != QLatin1String("none");
    actCopyImageTint_->setVisible(hasFilter);
    actSaveImageTint_->setVisible(hasFilter);
    actCopyImageCurrentRow_->setVisible(hasLines);
    actSaveImageCurrentRow_->setVisible(hasLines);
    syncSplitCopyDownloadSlot();
    // Qt will not enable an invisible action.
    actShareImage_->setEnabled(hasImg);
  }

  void MainWindow::onCanvasChanged() {
    refreshActions();
    onSelectionChanged();
    scheduleAutosave();
    remoteSync_->scheduleRemotePush();   // live co-edit: push the edit to the server for peers
    scheduleStencilAutosave();           // live file sync: auto-save the edit to the linked .stencil
  }

  // Live co-edit push/pull lives in RemoteSyncController (remoteSync_); the remote-link state and
  // reentrancy flags stay here.

  void MainWindow::onSelectionChanged() {
    // No image → no points panel, or restored lines float over the empty canvas.
    const bool hasImg = canvas_->hasImage();
    const core::Line* line = hasImg ? canvas_->panelLine() : nullptr;
    // Same pageCoords converter as the status bar and tooltip; mirrors
    // browser/js/core/coordTable.js.
    const auto u = unitFormat();
    // The unit rides in the column headings, not every cell (browser: `X cm` / `Y cm`), and tracks
    // the setting with no line on screen.
    selPanel_->setUnitLabel(QString::fromStdString(u.label));
    std::vector<SelectionPanel::PageRow> pageRows;
    if (line && canvas_->hasImage()) {
      pageRows.reserve(line->points.size());
      for (const auto& p : line->points) {
        const auto page = pageCoords(p.x, p.y);
        pageRows.push_back({QString::number(page.x * u.factor, 'f', 2),
                            QString::number(page.y * u.factor, 'f', 2)});
      }
    }
    selPanel_->showLine(line, hasImg ? canvas_->selectedPoint() : -1, pageRows);
    // Dust plays only on the hidden<->visible edge (browser selectionPanel.js wasHidden), never on
    // repopulating.
    const core::Line* editorLine = hasImg ? canvas_->selectedLine() : nullptr;
    const bool wasBarVisible = selectedLineDock_->isVisible();
    const bool showBar = editorLine != nullptr;
    selectedLineBar_->showLine(editorLine);
    // The Image Size dock's top margin drops to 0 while the bar is up, or the browser's gap
    // doubles.
    if (imageInfoHost_)
      if (auto* hostLay = imageInfoHost_->layout()) {
        QMargins m = hostLay->contentsMargins();
        m.setTop(showBar ? 0 : 8);
        hostLay->setContentsMargins(m);
      }
    if (showBar && !wasBarVisible) {
      selectedLineDock_->setVisible(true);
      // splitDockWidget against a hidden dock does not register, so re-affirm the stack once it is
      // on screen. Idempotent.
      if (imageInfoDock_) splitDockWidget(selectedLineDock_, imageInfoDock_, Qt::Vertical);
      if (QLayout* l = layout()) l->activate();   // the grab must see the shown bar, not a stale one
      dustSelectedLineBarIn();
    } else if (!showBar && wasBarVisible) {
      dustSelectedLineBarOut();
      selectedLineDock_->setVisible(false);
    } else {
      selectedLineDock_->setVisible(showBar);
    }
    selPanel_->setMultiSelectCount(hasImg ? canvas_->selectionCount() : 0);
    if (hasImg) selPanel_->setLines(canvas_->lines(), canvas_->selectedIndices());
    else selPanel_->setLines({}, {});
  }

  // Mirrors browser/js/ui/contextMenu.js grouping, reusing the shared QActions.
  void MainWindow::showContextMenuFromKeyboard() {
    if (!scroll_) return;
    const QWidget* vp = scroll_->viewport();
    const QRect vpGlobal(vp->mapToGlobal(QPoint(0, 0)), vp->size());
    const QPoint cursor = QCursor::pos();
    showContextMenu(vpGlobal.contains(cursor) ? cursor : vpGlobal.center());
  }

}  // namespace stencil::gui
