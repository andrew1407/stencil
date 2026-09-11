#include "mainWindow.hpp"
#include <QLabel>
#include <QMenuBar>
#include "mainWindow.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "chatMenuPanel.hpp"
#include "openImageDialog.hpp"
#include "canvasWidget.hpp"
#include "projectsDialog.hpp"
#include "selectionPanel.hpp"
#include "shortcutsDialog.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/controlReveal.hpp"

#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>

// Fullscreen: the toggle and the edge-hover reveal of the bars it hid.

namespace stencil::gui {

  void MainWindow::toggleFullscreen() {
    // Whatever is in the air goes first, both ways: the switch hides (or brings back) every
    // toolbar, and a cloud started by one of those controls was left flying over the bare
    // canvas, belonging to nothing.
    stopDustClouds(this);
    if (fs_.active) {
      // Exit: stop the hover poll and restore the top menu + points panel (right, as before).
      fs_.active = false;
      syncFullscreenGlyph();
      markFullscreenBars(false);
      if (fs_.hoverTimer) fs_.hoverTimer->stop();
      // Cancel any in-flight edge-hover slide and release the pinned toolbar heights so the restore
      // below starts from a clean state (a leftover animation / fixed height would fight it).
      if (barsAnim_) { barsAnim_->stop(); barsAnim_->deleteLater(); barsAnim_ = nullptr; }
      for (QToolBar* b : findChildren<QToolBar*>()) { b->setMinimumHeight(0); b->setMaximumHeight(QWIDGETSIZE_MAX); }
      fs_.barsShown = false;
      fs_.panelShown = false;
      beginFullscreenZoom();
      showNormal();
      if (menuBar()) menuBar()->setVisible(true);
      if (status_) status_->setVisible(true);     // restore the coord readout
      if (dropHint_) dropHint_->setVisible(true);  // …and the drag & drop hint (browser: .drop-hint)
      // …and the size readout, DOCK included: its height is pinned, so hiding only the bar
      // inside it left an empty band above the canvas.
      if (imageInfoHost_) imageInfoHost_->setVisible(true);
      if (imageInfoDock_) imageInfoDock_->setVisible(true);
      // The header row (Controls pill + project name) ALWAYS returns — it's the only way to re-show
      // the tool rows, so it must never stay hidden. The tool rows restore to their pre-fullscreen
      // shown/collapsed state (setToolbarsShown keeps the header, unlike setToolbarsVisible).
      if (headerToolbar_) { headerToolbar_->setMaximumHeight(QWIDGETSIZE_MAX); headerToolbar_->setVisible(true); }
      setToolbarsShown(fs_.wasToolbars, false);
      // Sync the toggle-action checks WITHOUT re-firing their (animated) toggled handlers, then
      // restore the panel to its pre-fullscreen expanded/collapsed(rail) state (non-animated).
      if (actToolbars_) { QSignalBlocker b(actToolbars_); actToolbars_->setChecked(fs_.wasToolbars); }
      if (actPanel_) { QSignalBlocker b(actPanel_); actPanel_->setChecked(fs_.wasPanel); }
      setPanelShown(fs_.wasPanel, false);
      positionOverlayArrows();
    } else {
      // Enter: hide the top menu (all toolbars + menubar) AND the points panel so the canvas fills
      // the screen; a cursor poll re-reveals the toolbars when the cursor touches the TOP edge and
      // the points panel (kept on the RIGHT) when it touches the RIGHT edge — mirrors the browser.
      fs_.wasToolbars = actToolbars_ ? actToolbars_->isChecked() : true;
      fs_.wasPanel = actPanel_ ? actPanel_->isChecked() : true;   // was the panel expanded (vs rail)?
      // Capture the panel's real width BEFORE hiding it, so the edge-hover reveal slides to exactly
      // that width instead of setPanelShown's 320px default — which would overshoot the panel's natural
      // width and snap back at the end of the slide (a visible jump).
      if (selPanel_->isVisible() && selPanel_->width() > 120) panelRestoreWidth_ = selPanel_->width();
      setToolbarsVisible(false);
      if (menuBar()) menuBar()->setVisible(false);
      if (status_) status_->setVisible(false);     // hide the coord readout so the canvas fills the screen
      if (dropHint_) dropHint_->setVisible(false);  // …and the hint (browser: .fullscreen-mode .drop-hint)
      // …and the image-size readout, which belongs to the tool rows: in fullscreen the
      // browser shows the canvas and nothing else, and this line sat over it. It comes
      // back with the rows on exit (refreshStatusHintVisibility).
      if (imageInfoHost_) imageInfoHost_->setVisible(false);
      if (imageInfoDock_) imageInfoDock_->setVisible(false);   // …its pinned band with it
      if (selPanel_->isVisible() && selPanel_->width() > 120) panelRestoreWidth_ = selPanel_->width();
      selPanel_->setVisible(false);   // hidden in fullscreen; revealed on right-edge hover
      fs_.barsShown = false;
      fs_.panelShown = false;
      fs_.active = true;
      syncFullscreenGlyph();
      markFullscreenBars(true);
      beginFullscreenZoom();
      showFullScreen();
      setFocus(Qt::OtherFocusReason);   // help key events reach us for the Escape-exits path
      if (fs_.hoverTimer) fs_.hoverTimer->start(16);   // ~60Hz poll: reveal reacts immediately on hover
    }
    // Reflect the on/off state on the toolbar button (accent fill via QToolButton:checked).
    // Guarded so it never re-enters through a toggled slot.
    if (actFullscreen_) { QSignalBlocker b(actFullscreen_); actFullscreen_->setChecked(fs_.active); }
  }

  // Fullscreen edge-hover reveal: show the toolbars while the cursor is at/over the TOP band, and
  // the points panel while it's at/over the RIGHT edge; hide each once the cursor leaves. Hysteresis
  // (a wide "keep" zone once shown) stops flicker as the cursor moves onto the revealed widget.
  void MainWindow::fsHoverTick() {
    if (!fs_.active) return;
    const QPoint p = mapFromGlobal(QCursor::pos());
    const QSize win = size();
    if (!FullscreenController::cursorInside(p, win)) return;
    // Top toolbars: slide them in (all rows incl. header — fullscreen hid them) when the cursor enters
    // the top band, keep them while it stays within the taller keep-zone. Drive off the tracked target
    // (fs_.barsShown), NOT live isVisible(): during an animated hide the bars stay visible until the
    // slide ends, so reading isVisible() here would restart the hide every 50ms tick (that's flicker).
    // The keep-zone is the REVEALED ROWS THEMSELVES, not a guessed band: at a fixed 150px
    // the cursor left the zone while still ON the lower rows and the menu slid shut under
    // it — before it had even reached the row it was heading for. Measured
    // from the visible tool rows (plus a little grace below them, so crossing a 1px gap
    // between rows never counts as leaving), and never smaller than the reveal band.
    int tbBottom = 0;
    for (QToolBar* b : findChildren<QToolBar*>())
      if (b != headerToolbar_ && b->isVisible())
        tbBottom = std::max(tbBottom, b->mapTo(this, QPoint(0, b->height())).y());
    const bool wantTb = fs_.wantBars(p, tbBottom);
    if (wantTb != fs_.barsShown) {
      fs_.barsShown = wantTb;
      // The TOOL rows only — the header row (logo + project name + the Controls pill) stays
      // away for the whole session, as the browser's fullscreen does: it shows the control
      // sections and nothing else (user decision, with a picture). It is also what stranded
      // the logo's mark, which is painted by an overlay that the row's slide clips away.
      QList<QToolBar*> bars;
      for (QToolBar* b : findChildren<QToolBar*>())
        if (b != headerToolbar_) bars.append(b);
      animateBarsHeight(bars, wantTb);   // reuse the pill's smooth height slide
    }

    // Right points panel: reveal it when the cursor hits the right edge (a generous 28px band, not
    // 5px which was near-impossible to hit). Once shown, KEEP it shown while the cursor stays in the
    // right third of the window — a wide keep-zone so dragging the dock splitter to RESIZE the panel
    // (setPanelShown's finish() releases the fixed width, so the splitter is draggable) doesn't stray
    // out of the zone and auto-hide the panel mid-drag.
    // …and the panel's keep-zone is the panel itself, the same way: its own width plus the
    // grace, never narrower than a third of the window (the splitter-drag allowance).
    const int panelW = selPanel_->isVisible() ? selPanel_->width() : 0;
    const bool wantPnl = fs_.wantPanel(p, win, panelW);
    if (wantPnl != fs_.panelShown) {
      fs_.panelShown = wantPnl;
      setPanelShown(wantPnl, true);
    }
  }

}  // namespace stencil::gui
