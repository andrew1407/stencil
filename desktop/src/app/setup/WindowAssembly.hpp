#pragma once

namespace stencil::gui {

  class MainWindow;

  // The constructor's phases: the canvas area and its filters, the docks, the chat dock and its
  // session, the overlays, the page and zoom controls, the sync controllers, the signal wiring
  // and the persisted state. It connects and routes to the window and its parts, nothing more.
  class WindowAssembly {
   public:
    explicit WindowAssembly(MainWindow& w) : w(w) {}

    void setupCanvasArea();
    void installWindowFilters();
    void setupDocks();
    void setupChatDock();
    void installPanelShimmers();
    void setupOverlaysAndStatus();
    void setupPageAndZoomControls();
    void setupSyncControllers();
    void wireSignals();
    void restorePersistedState(bool restoreLast);

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
