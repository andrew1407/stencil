// MainWindow construction, phase 2 of 4: the chat dock and its signals. Order is pinned; see
// WindowAssembly.cpp.
#include "MainWindow.hpp"
#include "SharedState.hpp"
#include "WindowAssembly.hpp"
#include "ChatMenuPanel.hpp"
#include "ChatPlanTarget.hpp"
#include "ChatSessionController.hpp"
#include "DockZonesOverlay.hpp"
#include "Notifications.hpp"
#include "SessionKey.hpp"
#include "SiblingWindows.hpp"

namespace stencil::gui {

  void WindowAssembly::setupChatDock() {
    // Dockable on all four sides + floating; hidden and docked left on every launch (browser
    // parity).
    w.chatDock = new ChatDock(&w);
    w.addDockWidget(Qt::LeftDockWidgetArea, w.chatDock);
    w.chatDock->hide();
    w.chatDock->setChatSwapSides(w.settings.chatSwapSides);   // the saved "Swap message sides" preference
    // Captured before the show/hide slide pins min==max; setChatShown restores it (a 0 would drop
    // the 260 px floor).
    w.parts.dockChrome.chatNaturalMin = QSize(w.chatDock->minimumWidth(), w.chatDock->minimumHeight());
    // Toasts and the resize-edge tint follow the dock; the rect alone does not say which side it
    // is on.
    QObject::connect(w.chatDock, &QDockWidget::dockLocationChanged, &w,
                     [this] { w.parts.dockChrome.syncToastInset(); w.parts.dockChrome.positionChatEdge(); });
    QObject::connect(w.chatDock, &QDockWidget::topLevelChanged, &w,
                     [this] { w.parts.dockChrome.syncToastInset(); w.parts.dockChrome.positionChatEdge(); });
    QObject::connect(w.chatDock, &QDockWidget::visibilityChanged, &w,
                     [this] { w.parts.dockChrome.syncToastInset(); w.parts.dockChrome.positionChatEdge(); });
    w.chatDock->installEventFilter(&w);
    w.chatSession = std::make_unique<ChatSessionController>(
        &w, w.canvas, w.chatDock, w.notify, w.settings, w.parts.chatAppliers.llmClient,
        ChatSessionController::Hooks{
            [this] { return w.parts.chatAppliers.currentLlmSettings(); },
            [this] { w.parts.chatAppliers.ensureLlmClient(); },
            [this]() -> std::unique_ptr<llm::PlanTarget> {
              return std::make_unique<ChatPlanTarget>(w);
            },
            [this](const QImage& image) {
              // The same entry the .stencil / server paths use; an empty layout means "just the picture".
              w.loadImageWithLayout(image, QJsonObject());
              w.playImageArrival();   // it lands on the canvas like any other fresh image
            },
            [this] {
              w.refreshActions();
              w.onSelectionChanged();
              w.updateImageSizeInfo();
              w.scheduleAutosave();
            },
            [this](const QImage& image, const QString& name) {
              return w.parts.chatAppliers.addImageProjectEntry(image, name, /*deferRegistrySave=*/true);
            },
            [this] {
              SharedState::instance().saveProjects(&w);
              SiblingWindows::refreshDockMenu(w.projectList);
            },
            [this] { w.refreshActions(); },
            [this] { w.parts.chatAppliers.persistActiveChat(); },
            [this] { w.parts.chatAppliers.clearPersistedChat(); },
            [this](const QString& path) { w.parts.chatAppliers.offerChatVideoUpload(path); },
            [this] { return w.parts.dockChrome.chatClosing; },
            [this] {
              if (w.acts.chat) w.acts.chat->setChecked(true);
              else if (w.chatDock) w.chatDock->setVisible(true);
            },
            [this](QRect anchorRect) { w.parts.dialogs.openAssistantSettingsFrom(nullptr, anchorRect); },
            [this] { w.persistSettings(); },
        });
    ChatSessionController* chat = w.chatSession.get();
    QObject::connect(w.chatDock, &ChatDock::sendRequested, chat, &ChatSessionController::onChatSend);
    QObject::connect(w.chatDock, &ChatDock::retryRequested, chat, &ChatSessionController::chatRetryTurn);
    QObject::connect(w.chatDock, &ChatDock::stopRequested, chat, &ChatSessionController::onChatStop);
    QObject::connect(w.chatDock, &ChatDock::reconnectRequested, &w,
                     [this](const QString&) { w.parts.projects.openConnections(); });
    // Every close on the dock leaves like the toolbar toggle: a docked chat slides into its edge,
    // a float flies to the icon.
    QObject::connect(w.chatDock, &ChatDock::closeRequested, &w, [this] {
      if (!w.chatDock || w.tearingDown || !w.chatDock->isVisible()) return;
      w.pop.anchor.clear();   // a plain close is never a popover gesture
      if (w.acts.chat && w.acts.chat->isChecked()) {
        w.acts.chat->setChecked(false);   // its handler runs setChatShown(false, animate)
        return;
      }
      w.parts.dockChrome.setChatShown(false, /*animate=*/true);
    });
    QObject::connect(w.chatDock, &ChatDock::clearRequested, chat, &ChatSessionController::onChatClear);
    // Every note the dock displays is mirrored onto the menu panel, late notes included.
    QObject::connect(w.chatDock, &ChatDock::notePosted, chat, [chat](const QString& text) {
      chat->chatMirror(QStringLiteral("Note"), text, true);
    });
    QObject::connect(w.chatDock, &ChatDock::toastRequested, &w, [this](const QString& text, bool failure) {
      if (!w.notify) return;
      if (failure) w.notify->error(text);
      else w.notify->info(text);
    });
    QObject::connect(w.chatDock, &ChatDock::lateNotePosted, chat, &ChatSessionController::chatMirrorLateNote);
    // A tear-off mid-slide would carry the pinned min==max extent into the floating window.
    QObject::connect(w.chatDock, &QDockWidget::topLevelChanged, &w, [this](bool) { w.parts.dockChrome.stopChatAnim(); });
    // A deliberate layout choice adopts the current shape (browser chatPanel adoptLayout parity).
    QObject::connect(w.chatDock, &ChatDock::dockRequested, &w,
                     [this] { w.parts.dockChrome.setChatCompactPopover(false); });
    QObject::connect(w.chatDock, &QDockWidget::dockLocationChanged, &w,
                     [this](Qt::DockWidgetArea) { w.parts.dockChrome.setChatCompactPopover(false); });
    QObject::connect(w.chatDock, &ChatDock::titleDragStarted, &w,
                     [this] { w.parts.dockChrome.setChatCompactPopover(false); });
    QObject::connect(w.chatDock, &ChatDock::dockRequested, &w,
                     [this](Qt::DockWidgetArea area) { w.parts.dockChrome.dockChatTo(area); });
    QObject::connect(w.chatDock, &ChatDock::floatToggleRequested, &w, [this] { w.parts.dockChrome.toggleChatFloat(); });
    // Edge drop bands for the whole title-bar drag; the release position decides (browser parity).
    QObject::connect(w.chatDock, &ChatDock::titleDragStarted, &w, [this] {
      w.parts.dockChrome.showChatDockZones([this] { return w.chatDock && w.chatDock->getDragActive(); });
    });
    QObject::connect(w.chatDock, &ChatDock::titleDragMoved, &w, [this](const QPoint& g) {
      if (w.overlays.dockZones && w.overlays.dockZones->isVisible())
        static_cast<DockZonesOverlay*>(w.overlays.dockZones)->dragTo(g);
    });
    QObject::connect(w.chatDock, &ChatDock::titleDragFinished, &w,
                     [this](const QPoint& g) {
      if (!w.overlays.dockZones || !w.overlays.dockZones->isVisible()) return;
      auto* zones = static_cast<DockZonesOverlay*>(w.overlays.dockZones);
      const int z = zones->zoneAt(g);
      w.overlays.dockZones->hide();
      if (z < 0) return;  // released outside every band → stay floating
      w.parts.dockChrome.dockChatTo(DockZonesOverlay::area(z));
    });
    QObject::connect(w.chatDock, &ChatDock::titleDragCanceled, &w, [this] {
      if (w.overlays.dockZones) w.overlays.dockZones->hide();
    });
    QObject::connect(w.chatDock, &ChatDock::videoAttached, chat, &ChatSessionController::onChatVideoAttached);
    QObject::connect(w.chatDock, &ChatDock::videoDetached, chat, [chat] {
      chat->chatVideoPath.clear();
      chat->chatVideoFrames = 0;
    });
    QObject::connect(w.chatDock, &ChatDock::openVariantRequested, &w,
                     [](const QString& id) { SiblingWindows::openProjectWindowById(id); });
    // The gear opens the assistant-only dialog (browser llmSettingsModal parity).
    QObject::connect(w.chatDock, &ChatDock::settingsRequested, &w,
                     [this] { w.parts.dialogs.openAssistantSettings(); });
    // A lambda, not a member pointer: the overload's defaulted QRect would not survive the
    // signal's QWidget*-only arity.
    QObject::connect(w.chatDock, &ChatDock::configureProviderRequested, &w,
                     [this](QWidget* anchor) { w.parts.dialogs.openAssistantSettingsFrom(anchor); });
    // A held, forgotten or lapsed anthropic key changes what the provider answers.
    QObject::connect(&llm::SessionKey::instance(), &llm::SessionKey::changed, &w, [this] {
      if (w.settings.llmProvider == QLatin1String("anthropic")) w.chatSession->refreshLlmStatus();
    });
    // The dock re-skinned itself already; persist and mirror to the menu panel, which has no
    // toggle of its own.
    QObject::connect(w.chatDock, &ChatDock::chatSwapSidesChanged, &w, [this](bool on) {
      w.settings.chatSwapSides = on;
      w.persistSettings();
      if (w.chatSession->chatMenuPanel) asChatMenu(w.chatSession->chatMenuPanel)->setChatSwapSides(on);
    });
  }

}  // namespace stencil::gui
