#include "mainWindow.hpp"
#include <QScrollArea>
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasWidget.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "numericInput.hpp"
#include "infoDialog.hpp"
#include "linksDialog.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "dataExportController.hpp"
#include "remoteSyncController.hpp"
#include "serverClient.hpp"
#include "selectionPanel.hpp"
#include "selectedLineBar.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "../support/controlSwap.hpp"
#include "../support/modalChrome.hpp"
#include "../support/iconMotion.hpp"

#include <QAction>
#include <QKeySequence>
#include <QLayout>

// Formula commit plus the action/selection state syncs that follow a canvas change.

namespace stencil::gui {

  // Validate fx/fy and apply them (the browser's settingsController
  // wireFormulaInputs commit). Reached when typing settles, on Enter / focus-out, or from
  // a programmatic set — never per keystroke, so a half-written expression is neither
  // applied nor flagged. Invalid expressions show the inline error and leave the last
  // good transform in force; valid ones persist and refresh the readout.
  void MainWindow::validateAndApplyFormulas() {
    // A focus-out commit also arrives while the window is being destroyed — Qt emits
    // editingFinished as the field loses focus, by which point the controllers this touches
    // are already gone. Same late-child-signal guard the chat dock uses.
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

  // While a split compare view is on, "With Compare" becomes the primary gesture: the
  // real Ctrl+C/Ctrl+Shift+D moves onto the split action (two enabled actions cannot
  // share a shortcut) and back onto "Current" when compare turns off.
  void MainWindow::syncSplitCopyDownloadSlot() {
    const bool split = canvas_->isSplitCompare();
    actCopyImageSplit_->setVisible(split);
    actSaveImageSplit_->setVisible(split);
    // Idempotent: once the shortcut has moved, the source action's shortcut() is already
    // empty, so re-running this on every refresh with no state change is a no-op.
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
    // "Current"'s OWN row (actCopyImageCurrentRow_/actSaveImageCurrentRow_) carries no
    // real shortcut of its own — its hint is a manual "\t"+combo suffix on its TEXT (the
    // same trick a submenu-opener row uses), kept mirroring whichever combo is actually
    // live on the primary action right now: Ctrl+C/Ctrl+Shift+D outside compare, nothing
    // while comparing (With Compare owns it then) — browser parity: contextMenu.js's own
    // ctx-copy-img-current-hk / ctx-dl-img-current-hk swap.
    auto setRowHint = [](QAction* row, QAction* primary) {
      const QString combo = primary->shortcut().isEmpty()
          ? QString() : primary->shortcut().toString(QKeySequence::NativeText);
      row->setText(QStringLiteral("Current (Tint + Lines/Points)") +
                   (combo.isEmpty() ? QString() : QStringLiteral("\t") + combo));
    };
    setRowHint(actCopyImageCurrentRow_, actCopyImage_);
    setRowHint(actSaveImageCurrentRow_, actSaveImage_);
  }

  // The export actions' shared gating (browser contextMenu.js syncState parity):
  // everything needs an image; "Filter Only" hides without a filter and "Current"'s own
  // row hides with nothing drawn (both would render byte-identical to a sibling row).
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
    // A no-op where there is no share sheet: Qt will not enable an invisible action.
    actShareImage_->setEnabled(hasImg);
  }

  void MainWindow::onCanvasChanged() {
    refreshActions();
    onSelectionChanged();
    scheduleAutosave();
    remoteSync_->scheduleRemotePush();   // live co-edit: push the edit to the server for peers
    scheduleStencilAutosave();           // live file sync: auto-save the edit to the linked .stencil
  }

  // Live co-edit push/pull (scheduleRemotePush / startRemotePoll / stopRemotePoll +
  // the poll/reload/live-feed internals) lives in RemoteSyncController (remoteSyncController.hpp),
  // constructed as remoteSync_. The remote-link state + the reentrancy flags stay here.

  void MainWindow::onSelectionChanged() {
    // No image → no points panel at all (restored lines from a prior session must not show
    // floating points over the empty "Open an image" canvas).
    const bool hasImg = canvas_->hasImage();
    const core::Line* line = hasImg ? canvas_->panelLine() : nullptr;
    // Per-point page (cm) coords via the same pageCoords converter the status bar
    // and tooltip use, so formulas + custom page apply identically in
    // the panel. Mirrors browser/js/core/coordTable.js (px + cm per point).
    const auto u = unitFormat();
    // The unit rides in the two page COLUMN HEADINGS, not in every cell — browser parity
    // (drawingApp.js relabels `X cm` / `Y cm`), and it must track the setting whether or
    // not a line is on screen.
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
    // The "Selected Line:" bar is gated on a real selection. Dust plays only on the
    // hidden<->visible edge (browser: selectionPanel.js wasHidden) — repopulating an
    // already-open bar (switching which line is selected) must not replay the gather.
    const core::Line* editorLine = hasImg ? canvas_->selectedLine() : nullptr;
    const bool wasBarVisible = selectedLineDock_->isVisible();
    const bool showBar = editorLine != nullptr;
    selectedLineBar_->showLine(editorLine);
    // The Image Size dock's own top margin lands between the "Selected Line:" bar's bottom
    // inset and the Image Size bar while the bar is up, doubling the browser's gap — drop it
    // to 0 then; the bar's own bottom inset (selectedLineBar.cpp) is the only breathing room
    // needed.
    if (imageInfoHost_)
      if (auto* hostLay = imageInfoHost_->layout()) {
        QMargins m = hostLay->contentsMargins();
        m.setTop(showBar ? 0 : 8);
        hostLay->setContentsMargins(m);
      }
    if (showBar && !wasBarVisible) {
      selectedLineDock_->setVisible(true);
      // Re-affirm the vertical stack now that selectedLineDock_ is actually on screen —
      // splitDockWidget against a HIDDEN dock (buildImageInfoBar ran before any selection
      // existed) doesn't cleanly register, leaving the two tiled side by side instead. Idempotent.
      if (imageInfoDock_) splitDockWidget(selectedLineDock_, imageInfoDock_, Qt::Vertical);
      if (QLayout* l = layout()) l->activate();   // the grab must see the shown bar, not a stale one
      dustSelectedLineBarIn();
    } else if (!showBar && wasBarVisible) {
      dustSelectedLineBarOut();
      selectedLineDock_->setVisible(false);
    } else {
      selectedLineDock_->setVisible(showBar);
    }
    // Gate the single-line editor with a "N lines selected" note while multi-selecting.
    selPanel_->setMultiSelectCount(hasImg ? canvas_->selectionCount() : 0);
    // Lines tab: every committed line, with the current selection highlighted (empty when
    // imageless, matching the points panel).
    if (hasImg) selPanel_->setLines(canvas_->lines(), canvas_->selectedIndices());
    else selPanel_->setLines({}, {});
  }

  // Canvas right-click menu — mirrors the grouping of browser/js/ui/contextMenu.js
  // (drawing · view/zoom · toggles · transform), reusing the shared QActions so
  // labels, checkmarks and enabled-state stay in sync with the toolbar/menubar.
  void MainWindow::showContextMenuFromKeyboard() {
    if (!scroll_) return;
    const QWidget* vp = scroll_->viewport();
    const QRect vpGlobal(vp->mapToGlobal(QPoint(0, 0)), vp->size());
    const QPoint cursor = QCursor::pos();
    showContextMenu(vpGlobal.contains(cursor) ? cursor : vpGlobal.center());
  }

}  // namespace stencil::gui
