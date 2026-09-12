// MainWindow construction, phase 2 of 4: the AI-assistant chat dock and every signal it
// raises — placement, the animated float/dock transitions, the drag dock zones, and the
// chat pipeline's own entry points. Order is pinned; see mainWindowSetupCanvas.cpp.
#include "mainWindow.hpp"
#include "chatDock.hpp"
#include "chatMenuPanel.hpp"
#include "dockZonesOverlay.hpp"
#include "selectionPanel.hpp"
#include "fileStore.hpp"
#include "notifications.hpp"
#include "theme.hpp"
#include <QDockWidget>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

namespace stencil::gui {

  void MainWindow::setupChatDock() {
    // AI-assistant chat dock: dockable on ALL four sides + free-floating
    // (deliberately unlike the pinned selection panel), hidden until the
    // toolbar/View toggle opens it. Docked LEFT by default (browser parity);
    // session-transient by design — every launch starts hidden at this default
    // placement (the windowState restore below resets it explicitly).
    chatDock_ = new ChatDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, chatDock_);
    chatDock_->hide();
    chatDock_->setChatSwapSides(settings_.chatSwapSides);   // the saved "Swap message sides" preference
    // The dock's own minimum, captured BEFORE the show/hide slide ever pins
    // min==max on it — setChatShown restores exactly this instead of releasing
    // to 0 (which would drop the dock's 260 px floor).
    chatNaturalMin_ = QSize(chatDock_->minimumWidth(), chatDock_->minimumHeight());
    // Toasts dodge the docked chat (syncToastInset); resize rides eventFilter.
    // …and the resize-edge tint moves with it — the dock's rect alone does not say which
    // SIDE the separator is on, so every area/float/visibility change re-places it.
    connect(chatDock_, &QDockWidget::dockLocationChanged, this,
            [this] { syncToastInset(); positionChatEdge(); });
    connect(chatDock_, &QDockWidget::topLevelChanged, this,
            [this] { syncToastInset(); positionChatEdge(); });
    connect(chatDock_, &QDockWidget::visibilityChanged, this,
            [this] { syncToastInset(); positionChatEdge(); });
    chatDock_->installEventFilter(this);
    connect(chatDock_, &ChatDock::sendRequested, this, &MainWindow::onChatSend);
    // Retry from a failed turn's card: the same send path, ignored mid-turn.
    connect(chatDock_, &ChatDock::retryRequested, this, &MainWindow::chatRetryTurn);
    connect(chatDock_, &ChatDock::stopRequested, this, &MainWindow::onChatStop);
    // The expired card's CTA opens Connections, where the row offers the sign-in.
    connect(chatDock_, &ChatDock::reconnectRequested, this,
            [this](const QString&) { openConnections(); });
    // The title-bar X (and every other close on the dock) leaves the way the
    // toolbar toggle does: a docked chat slides into the edge it is docked to —
    // left/right shrink their width, top/bottom their height — and a float flies
    // back into the icon.
    connect(chatDock_, &ChatDock::closeRequested, this, [this] {
      if (!chatDock_ || tearingDown_ || !chatDock_->isVisible()) return;
      pop_.anchor.clear();   // a plain close is never a popover gesture
      if (actChat_ && actChat_->isChecked()) {
        actChat_->setChecked(false);   // its handler runs setChatShown(false, animate)
        return;
      }
      setChatShown(false, /*animate=*/true);
    });
    // Title-bar trash: the dock wiped its transcript/attachments, we drop the
    // model-side conversation state that goes with it.
    connect(chatDock_, &ChatDock::clearRequested, this, &MainWindow::onChatClear);
    // Every note the dock DISPLAYS is mirrored onto the menu panel — including the
    // ones it posts on its own (a late note), or the panel runs a row short.
    connect(chatDock_, &ChatDock::notePosted, this, [this](const QString& text) {
      chatMirror(QStringLiteral("Note"), text, true);
    });
    // The attachment cap and its like: an accent toast, the browser's notify(…, 'info').
    connect(chatDock_, &ChatDock::toastRequested, this, [this](const QString& text) {
      if (notify_) notify_->info(text);
    });
    connect(chatDock_, &ChatDock::lateNotePosted, this, &MainWindow::chatMirrorLateNote);
    // Drag dock zones: edge drop bands over the CENTRAL dockable area (never
    // the toolbar/status chrome) for the WHOLE floating title-bar drag; the
    // release position decides (browser parity). dockChatTo (member function,
    // below) pins the chat to a side; wired here and from titleDragFinished/
    // toggleChatFloat.
    // When the chat stops sharing the panel's side (floated, closed, moved), the
    // panel would otherwise absorb the whole freed column — put it back to the
    // width it had before.
    // QPointer-guarded: these signals also fire while the window is being torn
    // down, when the docks may already be gone.
    const QPointer<QDockWidget> panelGuard(selPanel_);
    const QPointer<QDockWidget> chatGuard(chatDock_);
    const auto restorePanelWidth = [this, panelGuard, chatGuard] {
      if (tearingDown_ || !panelGuard || !chatGuard || panelGuard->isHidden()) return;
      if (!chatGuard->isHidden() && !chatGuard->isFloating() &&
          dockWidgetArea(chatGuard) == dockWidgetArea(panelGuard))
        return;   // still side by side — leave the split alone
      const int w = panelRestoreWidth_ > 120 ? panelRestoreWidth_ : kPanelDefaultWidth;
      QTimer::singleShot(0, this, [this, panelGuard, w] {
        if (panelGuard && !panelGuard->isHidden())
          resizeDocks({panelGuard.data()}, {w}, Qt::Horizontal);
      });
    };
    connect(chatDock_, &QDockWidget::topLevelChanged, this,
            [this, restorePanelWidth](bool) {
              // A tear-off mid-slide would otherwise carry the pinned min==max
              // extent into the floating window (and clamp its resize).
              stopChatAnim();
              restorePanelWidth();
            });
    connect(chatDock_, &QDockWidget::visibilityChanged, this,
            [restorePanelWidth](bool) { restorePanelWidth(); });
    connect(chatDock_, &QDockWidget::dockLocationChanged, this,
            [restorePanelWidth](Qt::DockWidgetArea) { restorePanelWidth(); });
    // Deliberate layout choices adopt the current shape — the transient
    // icon-popover flag stops applying (browser chatPanel adoptLayout parity).
    connect(chatDock_, &ChatDock::dockRequested, this,
            [this] { chatCompactPopover_ = false; });
    connect(chatDock_, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea) { chatCompactPopover_ = false; });
    connect(chatDock_, &ChatDock::titleDragStarted, this,
            [this] { chatCompactPopover_ = false; });
    // Title-bar placement buttons (browser parity): pin the dock to a side.
    connect(chatDock_, &ChatDock::dockRequested, this, &MainWindow::dockChatTo);
    // The title bar's own Float toggle: animated dock↔float, unlike setFloating() alone.
    connect(chatDock_, &ChatDock::floatToggleRequested, this, &MainWindow::toggleChatFloat);
    connect(chatDock_, &ChatDock::titleDragStarted, this, [this] {
      if (!dockZones_) dockZones_ = new DockZonesOverlay(this);
      // The bands span the whole DOCK REGION (window minus the top toolbars and
      // the status bar) — the browser's viewport equivalent. The central widget
      // is the wrong basis: it shrinks/offsets by whatever is currently docked
      // (the chat's own slot, the points panel), so bands based on it would not
      // sit on the real window edges.
      QRect target = rect();
      int top = 0;
      if (menuBar() && menuBar()->isVisible())
        top = qMax(top, menuBar()->geometry().bottom() + 1);
      for (QToolBar* tb : findChildren<QToolBar*>())
        if (tb->isVisible() && !tb->isFloating() && toolBarArea(tb) == Qt::TopToolBarArea)
          top = qMax(top, tb->geometry().bottom() + 1);
      int bottom = height() - 1;
      // findChild, not statusBar() — the accessor lazily CREATES a status bar on first call,
      // and this window no longer keeps one (the coord readout lives inline above the
      // drop-hint now, browser parity: #coord-status between .canvas-viewport and .drop-hint).
      if (auto* sb = findChild<QStatusBar*>(); sb && sb->isVisible())
        bottom = qMin(bottom, sb->geometry().top() - 1);
      if (bottom > top) {
        target.setTop(top);
        target.setBottom(bottom);
      }
      static_cast<DockZonesOverlay*>(dockZones_)->beginDrag(
          themePalette(resolveDark(settings_.themeMode), settings_.accentColor).accent,
          target, [this] { return chatDock_ && chatDock_->dragActive(); });
    });
    connect(chatDock_, &ChatDock::titleDragMoved, this, [this](const QPoint& g) {
      if (dockZones_ && dockZones_->isVisible())
        static_cast<DockZonesOverlay*>(dockZones_)->dragTo(g);
    });
    connect(chatDock_, &ChatDock::titleDragFinished, this,
            [this](const QPoint& g) {
      if (!dockZones_ || !dockZones_->isVisible()) return;
      auto* zones = static_cast<DockZonesOverlay*>(dockZones_);
      const int z = zones->zoneAt(g);
      dockZones_->hide();
      if (z < 0) return;  // released outside every band → stay floating
      dockChatTo(DockZonesOverlay::area(z));
    });
    connect(chatDock_, &ChatDock::titleDragCanceled, this, [this] {
      if (dockZones_) dockZones_->hide();
    });
    connect(chatDock_, &ChatDock::videoAttached, this, &MainWindow::onChatVideoAttached);
    // The video chip's × drops the video input (frame ops stop being valid).
    connect(chatDock_, &ChatDock::videoDetached, this, [this] {
      chatVideoPath_.clear();
      chatVideoFrames_ = 0;
    });
    connect(chatDock_, &ChatDock::openVariantRequested, this,
            [](const QString& id) { openProjectWindowById(id); });
    // The dock's single gear opens the dedicated, assistant-ONLY dialog
    // (browser llmSettingsModal parity) — not the full Settings sheet with the
    // LLM fields buried under theme/autosave/page size.
    connect(chatDock_, &ChatDock::settingsRequested, this,
            &MainWindow::openAssistantSettings);
    // The unreachable-provider card's "Configure provider" CTA: unlike the gear,
    // this button stays on screen through the click, so the dialog flies from it.
    // (A lambda, not a direct &MainWindow::openAssistantSettingsFrom pointer —
    // that overload now also takes a defaulted QRect, which the pointer's static
    // arity would carry into a signal that only supplies the QWidget*.)
    connect(chatDock_, &ChatDock::configureProviderRequested, this,
            [this](QWidget* anchor) { openAssistantSettingsFrom(anchor); });
    // "Swap message sides": the dock already re-skinned itself — persist the
    // choice and, if the context menu's mirror panel exists, keep it in step too
    // (chatMenuPanel.hpp: it has no toggle of its own, only the rendering).
    connect(chatDock_, &ChatDock::chatSwapSidesChanged, this, [this](bool on) {
      settings_.chatSwapSides = on;
      fileStore::saveSettings(settings_);
      if (chatMenuPanel_) asChatMenu(chatMenuPanel_)->setChatSwapSides(on);
    });
  }

}  // namespace stencil::gui
