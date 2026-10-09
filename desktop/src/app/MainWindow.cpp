// The window itself: the order its parts build it in, and teardown. The parts are in WindowParts.hpp;
// the hub methods they share are MainWindow*.cpp across app/.
#include "MainWindow.hpp"
#include "ChatSessionController.hpp"
#include "ProjectTitleController.hpp"
#include "ArrowPanner.hpp"
#include "DataExportController.hpp"
#include "RemoteSyncController.hpp"
#include "ProjectTransferController.hpp"
#include "StencilFileSync.hpp"
#include "SharedState.hpp"
#include "SiblingWindows.hpp"
#include "../support/tip/AppTooltip.hpp"   // the fading control tooltip

namespace stencil::gui {

  MainWindow::MainWindow(QWidget* parent, bool restoreLast)
      : QMainWindow(parent), projectList(SharedState::instance().getProjects()) {
    setWindowTitle("Stencil");
    setObjectName("stencilWindow");   // qss/app/shell.qss: the window's own separator is a hairline
    resize(1100, 760);
    setAcceptDrops(true);

    keys.load();  // must precede buildActions(), which reads the chords

    parts.assembly.setupCanvasArea();
    parts.assembly.installWindowFilters();

    parts.assembly.setupDocks();

    parts.assembly.setupChatDock();
    parts.assembly.installPanelShimmers();

    parts.assembly.setupOverlaysAndStatus();

    parts.assembly.setupPageAndZoomControls();

    parts.assembly.setupSyncControllers();

    parts.actionsBuilder.buildActions();
    parts.canvasMenu.buildContextActions();  // nested context-menu submenu actions
    parts.menuBuilder.buildMenus();
    parts.toolbarBuilder.buildToolbar();
    parts.exportMenus.wireExportOptionsPopups();  // needs the copy/save-image buttons buildToolbar() just made
    parts.dockChrome.buildOverlayArrows();   // sync the Controls-pill chevron glyph (after the toolbar exists)
    parts.actionsBuilder.bindRevealAnchors();    // every action records where its dialog should fly from

    // App-wide and idempotent: a second window installs nothing new.
    installAppTooltips();

    parts.assembly.wireSignals();

    parts.assembly.restorePersistedState(restoreLast);
  }

  // Defined here so unique_ptr members of forward-declared types see their complete type.
  // QWidget deletes children BEFORE QObject drops connections: teardown slots must bail on this flag.
  MainWindow::~MainWindow() {
    tearingDown = true;
    SiblingWindows::noteClosed(this);
    blocked.set(false);   // never leave the app-wide override pushed behind us
  }

}  // namespace stencil::gui
