#include "mainWindow.hpp"
#include "chatDock.hpp"
#include "stayOpenMenu.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "dockZonesOverlay.hpp"
#include "pillSplitter.hpp"
#include "colorNames.hpp"
#include "chatMenuPanel.hpp"
#include "opPlan.hpp"
#include "planExecutor.hpp"
#include "popover.hpp"
#include "qtLlmTransport.hpp"
#include "expirationDialog.hpp"
#include "deepLink.hpp"
#include "displayName.hpp"
#include "openImageDialog.hpp"
#include "../support/localPath.hpp"
#include "openInDialog.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "dropZonesOverlay.hpp"
#include "incognitoOverlay.hpp"
#include "projectDragZones.hpp"
#include "cropGeometry.hpp"
#include "geometry.hpp"
#include "imageFilter.hpp"
#include "pageMetrics.hpp"
#include "cropDialog.hpp"
#include "tooltipRows.hpp"
#include "zoomPan.hpp"
#include "guiHelpers.hpp"
#include "menuReveal.hpp"
#include "modalReveal.hpp"
#include "searchCombo.hpp"
#include "iconSet.hpp"
#include "numericInput.hpp"
#include "infoDialog.hpp"
#include "launchOptions.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "connectionStore.hpp"
#include "dataExportController.hpp"
#include "remoteSession.hpp"
#include "remoteSyncController.hpp"
#include "projectTransferController.hpp"
#include "liveFeed.hpp"
#include "serverClient.hpp"
#include "selectionPanel.hpp"
#include "assistantSettingsDialog.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "theme.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QBuffer>
#include <QCryptographicHash>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QEasingCurve>
#include <QEventLoop>
#include <QGraphicsOpacityEffect>

#include "../support/themeSwapOverlay.hpp"  // palette-swap wipe
#include "../support/disintegrateOverlay.hpp"  // the canvas scatters when cleared
#include "../support/shimmerOverlay.hpp"      // the shared hover sweep
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QShortcut>
#include <QShowEvent>
#include <QVariantAnimation>
#include <QIcon>
#include <QImage>
#include <QVariant>
#include <QImageReader>
#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QSplitter>
#include <QSplitterHandle>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPaintEvent>
#include <QAbstractItemView>
#include <QAbstractButton>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <QStyleHints>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QCloseEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QGridLayout>
#include <QKeyEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>
#include <QKeySequence>
#include <QPalette>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QShortcut>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QStyle>
#include <QToolButton>
#include <QFontDatabase>
#include <QLabel>
#include <QToolTip>
#include <QWidgetAction>
#include <algorithm>
#include <cmath>
#include <functional>

namespace stencil::gui {

  namespace {
    // Forward-declared here (defined lower in this TU's anon namespace) so the constructor's
    // ProjectTransferController fetchUrlBytes hook + openServerProject can name it.
    void fetchUrlBytesAsync(QObject* ctx, const QString& url, std::function<void(QByteArray)> done);

    // saveState/restoreState version — bumped when the toolbar rows change, so a state
    // saved against the old set is discarded instead of restoring the old row breaks.
    // 4: f(x,y) moved onto the Draw · View row and the SETTINGS cluster closes
    // Zoom · Page · Data. WITHOUT this bump a layout saved by an older build is
    // accepted verbatim, and it pins the old row extents — which left the new
    // SETTINGS section past the end of a restored row, invisible behind the
    // toolbar's "»" (a separator at the right edge and nothing after it).
    constexpr int kToolbarLayoutVersion = 4;

    // How long the chat takes to LEAVE: the dock's edge slide (setChatShown) and
    // a floating window's flight into the icon (modalReveal kCloseMs). The
    // compact popover waits this out so the two never overlap.
    constexpr int kChatSlideOutMs = 260;
    constexpr int kWindowDismissMs = 240;
    // The panel's two toggle chevrons read as one button, so they share a box (selectionPanel
    // kToggleBox/kToggleGlyph).
    constexpr int kPanelToggleBox = 24;
    constexpr int kPanelToggleGlyph = 15;
    constexpr int kPanelToggleInset = 3;   // gap between the floating chevron and the window edge
    constexpr int kPanelToggleTop = 5;     // and its drop below the toolbar edge
    constexpr int kFoldMs = 280;    // the toolbar/panel extent slides' duration
    // Idle pause after which the f(x,y) fields commit themselves. Mirrors the browser's
    // COMMIT_DEBOUNCE_MS (browser/js/ui/numericInput.js), which its formula pair shares.
    constexpr int kFormulaCommitMs = 1200;
  }  // namespace

  // App-lifetime macOS Dock menu, shared by all windows (see header note).
  QMenu* MainWindow::sDockMenu_ = nullptr;

  MainWindow::MainWindow(QWidget* parent, bool restoreLast)
      : QMainWindow(parent) {
    setWindowTitle("Stencil");
    resize(1100, 760);
    // Photoshop-style drop-to-open: a file dragged onto the window is opened
    // (image/video) or applied (layout JSON) via openPathFromOS / dropEvent.
    setAcceptDrops(true);

    loadHotkeys();  // must precede buildActions() (it calls hotkey(...))

    // ── central canvas in a scroll area ──
    canvas_ = new CanvasWidget(this);
    scroll_ = new QScrollArea(this);
    scroll_->setWidget(canvas_);
    scroll_->setAlignment(Qt::AlignCenter);
    scroll_->setFrameShape(QFrame::NoFrame);
    setCentralWidget(scroll_);
    // The canvas is sized to the image, so a zoomed-out image leaves margin around it
    // that belongs to the viewport, not the canvas. Filter the viewport so Ctrl+wheel /
    // trackpad pinch there still zoom (otherwise you can't zoom a small image back up).
    scroll_->viewport()->installEventFilter(this);
    // App-wide filter so Escape can leave fullscreen from any focus (see eventFilter).
    qApp->installEventFilter(this);

    selPanel_ = new SelectionPanel(this);
    // Named so QMainWindow::saveState() can persist/restore the dock layout
    // (unnamed docks are skipped with a warning).
    selPanel_->setObjectName("selectionPanelDock");
    addDockWidget(Qt::RightDockWidgetArea, selPanel_);
    // The selection panel owns the right area with a pinned width (setPanelShown drives
    // it via setFixedWidth during the slide animation). Without nesting, Qt offers no
    // drop slot in an area whose sole occupant can't resize — which made it impossible
    // to drag the chat dock onto the right side. Nesting restores those drop slots
    // (the chat dock stacks above/below the panel).
    setDockNestingEnabled(true);

    // AI-assistant chat dock: dockable on ALL four sides + free-floating
    // (deliberately unlike the pinned selection panel), hidden until the
    // toolbar/View toggle opens it. Docked LEFT by default (browser parity);
    // session-transient by design — every launch starts hidden at this default
    // placement (the windowState restore below resets it explicitly).
    chatDock_ = new ChatDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, chatDock_);
    chatDock_->hide();
    // The dock's own minimum, captured BEFORE the show/hide slide ever pins
    // min==max on it — setChatShown restores exactly this instead of releasing
    // to 0 (which would drop the dock's 260 px floor).
    chatNaturalMin_ = QSize(chatDock_->minimumWidth(), chatDock_->minimumHeight());
    // Toasts dodge the docked chat (syncToastInset); resize rides eventFilter.
    connect(chatDock_, &QDockWidget::dockLocationChanged, this,
            [this] { syncToastInset(); });
    connect(chatDock_, &QDockWidget::topLevelChanged, this, [this] { syncToastInset(); });
    connect(chatDock_, &QDockWidget::visibilityChanged, this, [this] { syncToastInset(); });
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
    // back into the icon. It used to call QWidget::close() and simply blink out.
    connect(chatDock_, &ChatDock::closeRequested, this, [this] {
      if (!chatDock_ || tearingDown_ || !chatDock_->isVisible()) return;
      popoverAnchor_.clear();   // a plain close is never a popover gesture
      if (actChat_ && actChat_->isChecked()) {
        actChat_->setChecked(false);   // its handler runs setChatShown(false, animate)
        return;
      }
      setChatShown(false, /*animate=*/true);
    });
    // Title-bar trash: the dock wiped its transcript/attachments, we drop the
    // model-side conversation state that goes with it.
    connect(chatDock_, &ChatDock::clearRequested, this, &MainWindow::onChatClear);
    // Every note the dock DISPLAYS is mirrored onto the menu panel — including
    // the ones it posts on its own (the attachment cap), which used to leave the
    // panel a row short of the dock.
    connect(chatDock_, &ChatDock::notePosted, this, [this](const QString& text) {
      chatMirror(QStringLiteral("Note"), text, true);
    });
    connect(chatDock_, &ChatDock::lateNotePosted, this, &MainWindow::chatMirrorLateNote);
    // Drag dock zones: edge drop bands over the CENTRAL dockable area (never
    // the toolbar/status chrome) for the WHOLE floating title-bar drag; the
    // release position decides (browser parity).
    // Pin the chat to a side. The selection panel already owns the right area,
    // so docking there must SPLIT side-by-side — plain addDockWidget stacks the
    // two vertically, giving each a squashed half-height column.
    const auto dockChatTo = [this](Qt::DockWidgetArea area) {
      const auto place = [this, area] {
        const bool shares = selPanel_ && !selPanel_->isHidden() &&
                            dockWidgetArea(selPanel_) == area &&
                            (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea);
        // Remember the panel's width BEFORE the split so it can be handed back
        // when the chat leaves again (otherwise it keeps the freed space).
        if (shares && selPanel_->width() > 120) panelRestoreWidth_ = selPanel_->width();
        addDockWidget(area, chatDock_);
        chatDock_->setFloating(false);
        if (shares) splitDockWidget(selPanel_, chatDock_, Qt::Horizontal);
      };
      // Moving between sides slides out of the old edge and back in at the new one —
      // the same extent slide the icon's open/close uses, so a placement change reads
      // as travel rather than a jump. Skipped when there is nothing on screen to move.
      const bool wasFloating = chatDock_->isFloating();
      if (tearingDown_ || !chatDock_->isVisible() || support::motionReduced()
          || (!wasFloating && dockWidgetArea(chatDock_) == area)) {
        place();
        return;
      }
      stopChatAnim();
      const bool horizNew = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
      const auto growIn = [this, horizNew] {
        const auto pin = [this, horizNew](int v) {
          if (horizNew) chatDock_->setFixedWidth(v); else chatDock_->setFixedHeight(v);
        };
        const int full = chatRestoreExtent_ > 80 ? chatRestoreExtent_ : (horizNew ? 345 : 320);
        pin(0);
        chatAnim_ = startExtentSlide(this, 0, full, 300, pin, [this] { stopChatAnim(); });
      };
      // Coming back from a FLOAT there is no edge to leave — it just slides in at the
      // side you picked (the browser plays its dock-in slide here too).
      if (wasFloating) {
        place();
        growIn();
        return;
      }
      const Qt::DockWidgetArea from = dockWidgetArea(chatDock_);
      const bool horizFrom = from != Qt::TopDockWidgetArea && from != Qt::BottomDockWidgetArea;
      const int extent = horizFrom ? chatDock_->width() : chatDock_->height();
      if (extent > 80) chatRestoreExtent_ = extent;   // come back at the size it had
      const auto pinFrom = [this, horizFrom](int v) {
        if (horizFrom) chatDock_->setFixedWidth(v); else chatDock_->setFixedHeight(v);
      };
      chatAnim_ = startExtentSlide(this, extent, 0, 200, pinFrom, [this, place, growIn] {
        stopChatAnim();          // release the pinned extent before re-docking
        place();
        growIn();
      });
    };
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
      const int w = panelRestoreWidth_ > 120 ? panelRestoreWidth_ : 320;
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
    connect(chatDock_, &ChatDock::dockRequested, this, dockChatTo);
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
      if (statusBar() && statusBar()->isVisible())
        bottom = qMin(bottom, statusBar()->geometry().top() - 1);
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
            [this, dockChatTo](const QPoint& g) {
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
    // The same hover shimmer the selection panel's buttons get.
    for (QAbstractButton* b : chatDock_->findChildren<QAbstractButton*>())
      installHoverShimmer(b);
    // Shared hover shimmer for the right Points/Lines panel: per-button on its buttons, and
    // per-ROW on its points table + lines list (item-view rows aren't widgets, so the overlay
    // tracks the hovered row) — matching the browser's coord-panel shimmer.
    for (QAbstractButton* b : selPanel_->findChildren<QAbstractButton*>()) installHoverShimmer(b);
    for (QAbstractItemView* v : selPanel_->findChildren<QAbstractItemView*>()) installRowShimmer(v);

    // Parented to the WINDOW, not the canvas viewport: the clear/paste effects
    // (DisintegrateOverlay) raise() themselves over that viewport, and a toast parented
    // there ended up painted UNDER the scattering motes. It also matches the browser,
    // whose #notify-balloon is position:fixed to the window rather than to the canvas.
    notify_ = new Notifications(this);
    // Server-project session domain (remoteSession.hpp): owns the remote-link state + the
    // ConnectionManager handle + the version-guarded write helpers. Created before the sync
    // controller (which composes it). Its ConnectionManager is set in ensureConnections().
    remoteSession_ = new RemoteSession(this, notify_);
    // Layout/image export + clipboard IO (dataExportController.hpp). Needs canvas_ + notify_ +
    // settings_, plus the project name + layout-meta accessors that stay on MainWindow.
    dataExport_ = std::make_unique<DataExportController>(
        this, canvas_, notify_, &settings_,
        [this] { return projectBaseName(); },
        [this] { return currentLayoutMeta(); });
    // Incognito indicator (dashed frame + badge) pinned to the canvas viewport,
    // mirroring the browser's body.incognito-mode outline/badge. Hidden until the
    // incognito action toggles it on.
    incognitoOverlay_ = new IncognitoOverlay(scroll_->viewport());
    // Split image-drop overlay (LEFT save / RIGHT incognito), shown while dragging a file.
    dropZones_ = new DropZonesOverlay(scroll_->viewport());
    // Accent lands in the theme apply below (QPalette::Highlight here was the OS
    // selection blue, not the app accent).
    // 3-zone overlay shown behind the Projects dialog while a project row is dragged out of it.
    projectZones_ = new ProjectDragZones(scroll_->viewport());
    tooltip_ = new CanvasTooltip(this);  // floating hover tooltip (S12)

    // Live cursor coord readout (Pixel/Page/To edge) at the bottom of the window — the desktop
    // equivalent of the browser's #coord-status bar below the canvas. It's empty while the cursor
    // is off the canvas (no "Ready" filler, matching the browser) and hidden during fullscreen.
    status_ = new QLabel(QString(), this);   // cursor readout only — blank until one hovers the canvas
    status_->setStyleSheet("font-family: monospace; padding: 0 6px;");
    statusBar()->addWidget(status_);

    // S10 custom page + the full ISO 216/269 A/B/C series. Items carry the
    // canonical value ("custom"/"A4") as DATA (read via pageSizeValue()); labels
    // add the physical size in the active display unit and are re-rendered by
    // applyUnitToPageCombo() when the unit changes. SearchComboBox opens the
    // browser-style themed popup with the pinned "Search…" filter (the port of
    // enhanceSelect({ search: true }) on #page-size) — the trigger itself stays
    // a plain, non-editable combo.
    pageSize_ = new SearchComboBox(this);
    fillPageSizeCombo(pageSize_, /*includeCustom=*/true);
    pageSize_->setToolTip("Page size");
    // The CLOSED combo is sized by the longest entry ("B0 (100 × 141.4 cm)"), which
    // made this the widest control on the row and pushed the SETTINGS cluster past
    // the window edge on an ordinary laptop screen. The dimensions are a reminder,
    // not the label — the popup (and the tooltip) still show them in full.
    pageSize_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    pageSize_->setMinimumContentsLength(11);
    pageSize_->setMaximumWidth(150);
    zoom_ = new QComboBox(this);
    zoom_->addItems({"10%", "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%", "500%", "800%", "1600%", "3200%"});
    zoom_->setToolTip("Zoom %");
    zoom_->setMaximumWidth(88);   // "3200%" plus the arrow; the rest was slack
    // Editable so the user can type an exact percent, but NoInsert so reflecting
    // a programmatic zoom (Ctrl+wheel) never appends list items — mirrors browser
    // zoomPan.js setZoom (a clamped numeric percent, never an accumulating list).
    zoom_->setEditable(true);
    zoom_->setInsertPolicy(QComboBox::NoInsert);
    zoom_->setCurrentText("100%");
    // Open the preset list as soon as the field is focused (click/tab), so a single control
    // offers BOTH typing and preset-picking without a separate dropdown gesture — the popup
    // still lets the user keep typing. Guarded by focus reason so it doesn't reopen when focus
    // returns from the just-closed popup (which would loop).
    zoom_->lineEdit()->installEventFilter(this);

    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setSingleShot(true);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::saveSessionNow);

    // Live co-edit push/pull engine (remoteSyncController.hpp): owns the debounce/poll/reload
    // timers + the LiveFeed. It composes remoteSession_ directly for the link state + connections;
    // only the reentrancy flags (two &-flags, now async-in-flight state) and the
    // syncToServer/incognito predicates plus saveToServer / openServerProject (both async) stay as
    // hooks here.
    remoteSync_ = std::make_unique<RemoteSyncController>(
        this, remoteSession_, &remoteReloading_, &remotePushing_,
        RemoteSyncController::Hooks{
            [this] { return settings_.syncToServer; },
            [this] { return incognito_; },
            [this] { saveToServer(); },
            [this](const QString& a, const QString& i, bool s) { openServerProject(a, i, s); },
            [this] {
              resetToBlankEditor();
              updateProjectTitle();
              notify_->info(QStringLiteral("This server project was deleted"));
            },
        });
    // Local↔server project transfer service (projectTransferController.hpp): operates on the
    // project list + store, reaching the session/UI it can't own through these hooks.
    projectTransfer_ = std::make_unique<ProjectTransferController>(
        notify_, canvas_, &settings_, &projectsStore_, &projectList_,
        ProjectTransferController::Hooks{
            [this] { return connections_; },
            [this](const std::string& id) { return findProject(id); },
            [this] { return currentLayoutMeta(); },
            [this](const QString& url, std::function<void(QByteArray)> done) {
              fetchUrlBytesAsync(this, url, std::move(done));
            },
            [this] { return activeProjectId_; },
            [this] { return remoteSession_->link().address; },
            [this] { return remoteSession_->link().id; },
            [this](const QString& serverUrl, const QString& newId, const QString& name,
                   const QString& color, qint64 version) {
              activeProjectId_.clear();
              remoteSession_->link().bind(serverUrl, newId, name, color, version);
              remoteSync_->startRemotePoll();
              updateProjectTitle();
            },
            [this](const QString& id) { loadProjectIntoCanvas(id); },
            [this] { refreshActions(); refreshDockMenu(); },
        });

    buildActions();
    buildContextActions();  // S11: nested context-menu submenu actions
    buildMenus();
    buildToolbar();
    buildOverlayArrows();   // sync the Controls-pill chevron glyph (after the toolbar exists)
    bindRevealAnchors();    // every action records where its dialog should fly from

    // ── wiring ── (after buildToolbar so the referenced widgets/actions exist)
    wireSignals();

    // ── load persisted state ──
    projectList_ = fileStore::loadProjects();
    settings_ = fileStore::loadSettings();
    applySettings(settings_, false);
    if (restoreLast) restoreSession();  // skipped for a blank incognito editor
    // Restore the dock layout saved by closeEvent. Toolbars are forced visible
    // afterwards — their collapse is session-transient (the Controls pill), not
    // persisted — and the panel toggle is re-synced to the restored visibility.
    // NOTE: isHidden(), not isVisible() — the window isn't shown yet, so
    // isVisible() is false for every child and would desync the toggle (the
    // "Hide panel" chevron then no-ops because the action is already unchecked).
    if (!settings_.windowState.isEmpty()) {
      // Versioned: a state saved against a DIFFERENT set of toolbars restores their old
      // row breaks and re-splits the rows. Bump on every toolbar restructure.
      restoreState(QByteArray::fromBase64(settings_.windowState.toLatin1()), kToolbarLayoutVersion);
      for (QToolBar* tb : findChildren<QToolBar*>()) tb->setVisible(true);
      if (actPanel_) {
        QSignalBlocker b(actPanel_);
        actPanel_->setChecked(!selPanel_->isHidden());
      }
      updatePanelReopenButton();
      // The chat dock is deliberately session-transient (browser parity: full
      // reset on reload): whatever an older saved layout says, it boots hidden
      // at its default left placement.
      if (chatDock_->isFloating()) chatDock_->setFloating(false);
      addDockWidget(Qt::LeftDockWidgetArea, chatDock_);
      chatDock_->hide();
      if (actChat_) {
        QSignalBlocker b(actChat_);
        actChat_->setChecked(false);
      }
    }
    // Auto-connect saved servers (if the preference is on) only for the primary restored
    // window; deferred so the window paints before the synchronous REST handshakes run.
    if (restoreLast)
      QTimer::singleShot(0, this, &MainWindow::autoConnectServers);
    refreshActions();
    onSelectionChanged();
    updateStatusIdle();
    refreshDockMenu();  // macOS Dock menu (no-op elsewhere)

    // Live OS-scheme follow: re-tint when the system scheme flips, but only while
    // we're in "system" mode (an explicit light/dark choice wins). The
    // colorSchemeChanged signal / Qt::ColorScheme arrived in Qt 6.5; on older Qt
    // the system theme is still applied at startup, just not followed live.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) {
              if (settings_.themeMode == "system") applyTheme();
            });
#endif
  }

  // Defined here (not =default in the header) so unique_ptr members of forward-declared
  // types are destroyed where their complete type is visible (dataExportController.hpp above).
  // QWidget deletes its children BEFORE QObject drops their connections, so a
  // dock's teardown signals can still reach a half-destroyed MainWindow; this
  // flag lets those slots bail out.
  MainWindow::~MainWindow() {
    tearingDown_ = true;
    setBlockedCursor(false);   // never leave the app-wide override pushed behind us
  }

  // ── hotkeys map (ported from browser/js/config/hotkeysConfig.json) ──
  // Defaults + labels from the embedded config, then user overrides layered on
  // top (override wins), mirroring the browser STORAGE_KEYS.hotkeys merge (S13).
  void MainWindow::loadHotkeys() {
    QFile hk(":/config/hotkeysConfig.json");
    if (hk.open(QIODevice::ReadOnly)) {
      for (const auto& v : QJsonDocument::fromJson(hk.readAll()).array()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value("id").toString();
        const QString def = o.value("default").toString();
        hotkeyDefaults_.insert(id, def);
        hotkeyLabels_.insert(id, o.value("label").toString());
        hotkeys_.insert(id, def);
      }
    }
    // Selected-line flip / rotate-90 chords (Alt+Shift+arrow). These fire from
    // keyPressEvent (like the Alt+R+arrow rotate), so they have no live QAction —
    // register them as defaults + labels only, so the shortcuts dialog still lists
    // them for discovery. Qt-style arrow tokens ("Up"…) so QKeySequence parses them.
    struct ChordDef { const char* id; const char* seq; const char* label; };
    static const ChordDef kLineTransformChords[] = {
        {"flipLineHorizontal", "Alt+Shift+Up", "Flip Selected Line Horizontal"},
        {"flipLineVertical", "Alt+Shift+Down", "Flip Selected Line Vertical"},
        {"rotateLineCW90", "Alt+Shift+Right", "Rotate Selected Line +90°"},
        {"rotateLineCCW90", "Alt+Shift+Left", "Rotate Selected Line −90°"},
    };
    for (const auto& c : kLineTransformChords) {
      hotkeyDefaults_.insert(c.id, c.seq);
      hotkeyLabels_.insert(c.id, c.label);
      hotkeys_.insert(c.id, c.seq);
    }
    const auto overrides = fileStore::loadHotkeys();
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
      hotkeys_.insert(it.key(), it.value());
  }

  // Signal wiring extracted from the ctor. The connect() ORDER is observable
  // (e.g. the allowFormulas_ handler drives actAllowFormulas_; customW_/customH_
  // handlers call onSelectionChanged) and is preserved verbatim here. Must run
  // after the widgets/actions are built and before the persisted-state load.
  void MainWindow::wireSignals() {
    // Keep the AI-Assistant toggle in lockstep with the dock (the dock's own ✕
    // close button, restoreState, tabbing — any visibility change re-syncs it),
    // and probe provider reachability whenever the dock opens.
    connect(chatDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
      // Shown again ⇒ definitively not leaving (whatever interrupted the slide).
      if (visible) { chatClosing_ = false; chatDock_->setClosing(false); }
      if (actChat_ && actChat_->isChecked() != visible) {
        QSignalBlocker b(actChat_);
        actChat_->setChecked(visible);
      }
      if (visible) refreshLlmStatus();
    });
    connect(canvas_, &CanvasWidget::hovered, this, &MainWindow::onHovered);
    connect(canvas_, &CanvasWidget::changed, this, &MainWindow::onCanvasChanged);
    connect(canvas_, &CanvasWidget::selectionChanged, this,
            &MainWindow::onSelectionChanged);
    connect(canvas_, &CanvasWidget::contextRequested, this,
            &MainWindow::showContextMenu);
    // Idle-canvas click (no image yet) opens the blank-image creator — growing out of
    // the CARD that was clicked, not the toolbar icon the action normally flies from.
    connect(canvas_, &CanvasWidget::blankImageRequested, this, [this] {
      const QRect card = canvas_ ? canvas_->idleCardGlobalRect() : QRect();
      if (card.isValid()) {
        dialogAnchor_.clear();       // the rect below is the origin, not any icon
        dialogAnchorRect_ = card;
      }
      openImageDialog(/*startBlank=*/true);
    });
    connect(canvas_, &CanvasWidget::zoomStep, this, &MainWindow::zoomStep);
    // Reflect drawing mode in the Start/Stop actions (S5).
    connect(canvas_, &CanvasWidget::drawingModeChanged, this,
            &MainWindow::refreshActions);
    // Hover tooltip (S12).
    connect(canvas_, &CanvasWidget::hoverDetail, this,
            &MainWindow::onHoverDetail);
    connect(canvas_, &CanvasWidget::hoverLeft, this,
            [this] { tooltip_->hide(); });
    // Pan / zoom interactions (S7/S8/S9).
    connect(canvas_, &CanvasWidget::panBy, this,
            [this](int dx, int dy, bool fast) {
              const double speed = fast ? 2.5 : 1.0;  // drawingApp.js pan speed
              scrollTo(
                  scroll_->horizontalScrollBar()->value() - qRound(dx * speed),
                  scroll_->verticalScrollBar()->value() - qRound(dy * speed));
            });
    connect(canvas_, &CanvasWidget::fitRequested, this, &MainWindow::fitToWindow);
    connect(canvas_, &CanvasWidget::zoomAtCursor, this,
            [this](int dir, const QPoint& posInWidget, bool fast) {
              // Step 0.1 (0.3 with Shift), additive, matching drawingApp.js wheel.
              const double step = fast ? 0.3 : 0.1;
              const double target = canvas_->scale() + dir * step;
              // posInWidget is canvas-space; convert to viewport coords for the
              // anchored-zoom focal math (subtract the canvas origin in the vp).
              const QPoint inVp =
                  canvas_->mapTo(scroll_->viewport(), posInWidget);
              setZoomAnchored(target, inVp);
            });
    connect(canvas_, &CanvasWidget::zoomByFactorAt, this,
            [this](double factor, const QPoint& posInWidget) {
              // Trackpad pinch: scale continuously about the cursor (same anchored
              // path as Ctrl+wheel, but a smooth factor rather than a fixed step).
              const QPoint inVp = canvas_->mapTo(scroll_->viewport(), posInWidget);
              setZoomAnchored(canvas_->scale() * factor, inVp);
            });
    connect(canvas_, &CanvasWidget::zoomToRect, this,
            [this](const QRectF& r) {
              const QSize vp = scroll_->viewport()->size();
              const auto z = core::rectZoom(r.x(), r.y(), r.width(), r.height(),
                                            vp.width(), vp.height());
              setZoom(z.scale);
              scrollTo(qRound(z.scrollLeft), qRound(z.scrollTop));
            });
    connect(zoom_, &QComboBox::currentTextChanged, this, [this](const QString& t) {
      // Accept an optional trailing "%"; parse the percent and apply (clamped in
      // setZoom). syncCombo=false so we don't re-write the field we're reading.
      QString s = t;
      s.remove('%');
      bool ok = false;
      const double pct = s.trimmed().toDouble(&ok);
      if (ok) setZoom(pct / 100.0, false);
    });
    // Page size + custom inputs (S10). Index-based (not text): the editable
    // search field mutates the text on every keystroke, but a page change is
    // only a change of the selected item.
    connect(pageSize_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onPageSizeChanged(); });
    connect(customW_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
              // Spinboxes are edited in the active unit; store the model in cm.
              settings_.customPageWidth = v / unitFormat().factor;
              persistSettings();
              onHovered(lastHoverX_, lastHoverY_);
              onSelectionChanged();  // refresh panel cm (S10/GAP-2)
              remoteSync_->scheduleRemotePush();  // page format rides the layout — push it to peers
            });
    connect(customH_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
              settings_.customPageHeight = v / unitFormat().factor;
              persistSettings();
              onHovered(lastHoverX_, lastHoverY_);
              onSelectionChanged();  // refresh panel cm (S10/GAP-2)
              remoteSync_->scheduleRemotePush();
            });
    // Formula controls (S11). The toolbar checkbox is the single source of
    // truth; the View ▸ Allow Formulas action just drives it (and is kept in
    // sync here), so the feature stays reachable when the toolbar overflows.
    connect(allowFormulas_, &QCheckBox::toggled, this, [this](bool on) {
      settings_.allowFormulas = on;
      if (formulaGroup_) formulaGroup_->setVisible(on);
      if (actAllowFormulas_ && actAllowFormulas_->isChecked() != on) {
        QSignalBlocker ba(actAllowFormulas_);
        actAllowFormulas_->setChecked(on);
      }
      // Toggling only shows/hides the inputs + gates whether formulas apply to the conversion
      // (pageCm passes allowFormulas) — the expressions are KEPT so re-enabling restores them.
      if (!on) formulaError_->setVisible(false);
      persistSettings();
      onHovered(lastHoverX_, lastHoverY_);
      onSelectionChanged();  // refresh panel cm when formulas toggle (GAP-2)
      remoteSync_->scheduleRemotePush();  // formulas ride the layout — push to peers
    });
    connect(actAllowFormulas_, &QAction::toggled, this,
            [this](bool on) { allowFormulas_->setChecked(on); });
    // The f(x,y) pair commits on an idle pause, not per keystroke (see the timer's
    // declaration): applying every intermediate expression re-rendered the readouts,
    // wrote settings and pushed a peer sync per character, and flashed the invalid
    // indicator mid-word. Enter / focus-out still apply at once.
    formulaCommitTimer_ = new QTimer(this);
    formulaCommitTimer_->setSingleShot(true);
    formulaCommitTimer_->setInterval(kFormulaCommitMs);
    connect(formulaCommitTimer_, &QTimer::timeout, this, [this] { validateAndApplyFormulas(); });
    const auto onFormulaEdited = [this](const QString&) {
      // Typing your way back to something valid clears a stale error at once; a wrong one
      // is only flagged once you stop, so "(x" mid-expression doesn't flash red.
      const bool okX = core::FormulaParser::validate(formulaX_->text().trimmed().toStdString(), 'x');
      const bool okY = core::FormulaParser::validate(formulaY_->text().trimmed().toStdString(), 'y');
      if (okX && okY) formulaError_->setVisible(false);
      formulaCommitTimer_->start();
    };
    connect(formulaX_, &QLineEdit::textChanged, this, onFormulaEdited);
    connect(formulaY_, &QLineEdit::textChanged, this, onFormulaEdited);
    connect(formulaX_, &QLineEdit::editingFinished, this, [this] { validateAndApplyFormulas(); });
    connect(formulaY_, &QLineEdit::editingFinished, this, [this] { validateAndApplyFormulas(); });
    connect(selPanel_, &SelectionPanel::pointActivated, this,
            [this](int i) { canvas_->selectPoint(i); });
    connect(selPanel_, &SelectionPanel::pointDeleteRequested, this,
            [this](int i) { canvas_->deletePoint(i); });
    connect(selPanel_, &SelectionPanel::pointCoordChanged, this,
            [this](int i, int axis, double v) { canvas_->setPointCoord(i, axis, v); });

    // ── Hover cross-highlight (browser parity, both directions). List rows → canvas
    // ring/glow; canvas cursor → list row tints (never scrolls a list).
    connect(selPanel_, &SelectionPanel::pointRowHovered, this,
            [this](int i) { canvas_->setListHoverPoint(i); });
    connect(selPanel_, &SelectionPanel::lineRowHovered, this,
            [this](int i) { canvas_->setListHoverLine(i); });
    connect(canvas_, &CanvasWidget::canvasHoverChanged, this,
            [this](int lineIdx, int ptIdx, int overLineIdx) {
              // The points table shows panelLine(); only its own points tint a row.
              const bool onPanelLine = ptIdx >= 0 && lineIdx == canvas_->panelLineIdx();
              selPanel_->setCanvasHover(onPanelLine ? ptIdx : -1, overLineIdx);
            });

    // ── selection-panel inline line editor → canvas mutators (Step 10).
    // Mirrors browser/js/core/drawingApp.js:181-195 applySelectionChange /
    // applyFill / deselectLine wiring; the SelectionPanel owns inline line
    // editing (PARITY_PLAN3 conflict-resolution: toolbar sets defaults only).
    connect(selPanel_, &SelectionPanel::lineColorChanged, this,
            [this](const QString& c) { canvas_->setSelectedLineColor(c); });
    connect(selPanel_, &SelectionPanel::linePointColorChanged, this,
            [this](const QString& c) { canvas_->setSelectedLinePointColor(c); });
    connect(selPanel_, &SelectionPanel::lineThicknessChanged, this,
            [this](int t) { canvas_->setSelectedLineThickness(t); });
    connect(selPanel_, &SelectionPanel::linePointSizeChanged, this,
            [this](int m) { canvas_->setSelectedLinePointSize(m); });
    connect(selPanel_, &SelectionPanel::lineStyleChanged, this,
            [this](const QString& s) { canvas_->setSelectedLineStyle(s); });
    connect(selPanel_, &SelectionPanel::lineFillChanged, this,
            [this](const QString& f) { canvas_->setSelectedLineFill(f); });
    connect(selPanel_, &SelectionPanel::lineDeleteRequested, this,
            [this] { canvas_->deleteSelectedLine(); });
    connect(selPanel_, &SelectionPanel::deselectRequested, this,
            [this] { canvas_->deselect(); });
    // Panel header chevron → hide the panel (routes through actPanel_ so the View menu / Alt+X and
    // the re-open tab stay in sync). The animated slide runs from setPanelShown.
    connect(selPanel_, &SelectionPanel::collapseRequested, this,
            [this] { if (actPanel_) actPanel_->setChecked(false); });
    selPanel_->setToggleHint(hotkey("togglePointsList", "Alt+X"));   // shortcut in the chevron tooltip

    // ── Lines tab (SelectionPanel) → canvas index-keyed selection/removal.
    // Click a row to single-select (Ctrl/⌘+Shift toggles multi-select); its 🗑 removes it.
    connect(selPanel_, &SelectionPanel::lineListActivated, this,
            [this](int idx, bool multi) {
              if (multi) canvas_->toggleLineSelectionByIndex(idx);
              else canvas_->selectLineByIndex(idx);
            });
    connect(selPanel_, &SelectionPanel::lineListRemoveRequested, this,
            [this](int idx) { canvas_->removeLineByIndex(idx); });
  }

  QString MainWindow::hotkey(const QString& id, const QString& fallback) const {
    return hotkeys_.value(id, fallback);
  }

  // Push the current default visuals to the canvas and persist (S8). Mirrors the
  // browser change handlers that update this.color/thickness/pointSize/style then
  // storage.save() (drawingApp.js:155-178). Defaults ONLY — never the selection.
  QColor MainWindow::effectiveDefaultPointColor() const {
    const QColor c(settings_.defaultPointColor);
    return (!settings_.defaultPointColor.isEmpty() && c.isValid()) ? c : lineColorValue_;
  }

  void MainWindow::onLineStyleControlChanged() {
    canvas_->setDefaults(settings_.defaultColor, settings_.defaultThickness,
                         settings_.defaultPointSize, settings_.defaultStyle,
                         settings_.defaultPointColor);
    persistSettings();
  }

  // ── Shared apply paths for controls duplicated in the toolbar AND context menu.
  // Both UIs route through these so they never drift (the toolbar combo and the
  // context-menu radio group stay mutually in sync) and the apply/persist logic
  // lives once. Setting an exclusive QAction's checked state emits toggled(), not
  // triggered(), so re-checking the group action here never re-enters this path.
  // Single entry for a compare-mode change: apply to the canvas and keep the toolbar
  // combo + View → Compare submenu radio set in sync. Transient view state — not persisted.
  void MainWindow::setCompareModeUi(const QString& mode) {
    canvas_->setCompareMode(mode);
    if (compareCombo_) {
      const int idx = compareCombo_->findData(mode);
      if (idx >= 0 && idx != compareCombo_->currentIndex()) {
        QSignalBlocker b(compareCombo_);
        compareCombo_->setCurrentIndex(idx);
      }
    }
    if (compareGroup_) {
      for (QAction* a : compareGroup_->actions())
        if (a->data().toString() == mode) { a->setChecked(true); break; }
    }
    refreshActions();   // read-only view gates the editing actions + their shortcuts
  }

  void MainWindow::applyImageFilter(const QString& mode) {
    settings_.imageFilter = mode;
    if (imageFilter_) {  // sync toolbar combo by canonical data value
      const int idx = imageFilter_->findData(mode);
      if (idx >= 0) {
        QSignalBlocker b(imageFilter_);
        imageFilter_->setCurrentIndex(idx);
      }
    }
    if (filterButtons_) {  // sync context-menu radio group (blocked so it doesn't re-apply)
      for (QAbstractButton* b : filterButtons_->buttons())
        if (b->property("filterValue").toString() == mode) {
          QSignalBlocker bl(b);
          b->setChecked(true);
          break;
        }
    }
    if (filterColorBtn_) filterColorBtn_->setVisible(mode == "custom");
    canvas_->setImageFilter(mode, filterColorValue_);
    persistSettings();
    if (!remoteReloading_) filterDirty_ = true;   // user changed the filter
    remoteSync_->scheduleRemotePush();   // live co-edit: a filter change isn't a canvas changed()
  }

  void MainWindow::applyTintColor(const QColor& color) {
    filterColorValue_ = color;
    settings_.filterColor = color.name(QColor::HexRgb);
    if (filterColorBtn_) updateColorSwatch(filterColorBtn_, color);
    canvas_->setImageFilter(settings_.imageFilter, filterColorValue_);
    persistSettings();
    if (!remoteReloading_) filterDirty_ = true;   // user changed the tint
    remoteSync_->scheduleRemotePush();   // live co-edit: push tint changes to peers
  }

  void MainWindow::applyLineStyle(const QString& style) {
    settings_.defaultStyle = style;
    if (lineStyle_) {  // sync toolbar combo by canonical data value
      const int idx = lineStyle_->findData(style);
      if (idx >= 0) {
        QSignalBlocker b(lineStyle_);
        lineStyle_->setCurrentIndex(idx);
      }
    }
    if (lineStyleGroup_) {  // sync context-menu radio group
      for (QAction* a : lineStyleGroup_->actions())
        if (a->data().toString() == style) { a->setChecked(true); break; }
    }
    onLineStyleControlChanged();
  }

  // Paint a flat color chip as the toolbutton's icon so swatches read as their
  // current color (S8; the browser uses <input type=color>).
  void MainWindow::updateColorSwatch(QToolButton* btn, const QColor& color) {
    // Input-style chip for the Style-row colour pickers: the SAME shared input
    // palette (background + border) as the spinboxes/combo beside it, with the
    // colour swatch drawn INSIDE — not a colour-on-white chip that clashes with
    // the dark inputs. Re-run from applyTheme, so it tracks light/dark.
    const Palette pal =
        themePalette(resolveDark(settings_.themeMode), settings_.accentColor);
    // A labelled chip (the "Blank" swatch) keeps its caption beside the colour;
    // bare chips stay the fixed 46×26 input shape.
    const bool labelled = !btn->text().isEmpty();
    btn->setFixedHeight(26);
    if (labelled) btn->setMinimumWidth(46);
    else btn->setFixedWidth(46);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(
        QStringLiteral(
            "QToolButton{background:%1;border:1px solid %2;border-radius:7px;"
            "color:%4;padding:0 %5px;}"
            "QToolButton:hover{border-color:%3;}")
            .arg(pal.inputBg.name(), pal.borderMain.name(), pal.accent.name(),
                 pal.textMain.name(), labelled ? QStringLiteral("6") : QStringLiteral("0")));
    // The swatch: a rounded colour rect with a soft luminance-tuned outline so
    // a colour close to the input background stays visible in either theme.
    QPixmap pm(32, 16);
    pm.fill(Qt::transparent);
    {
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing);
      const bool lightFill = color.lightnessF() > 0.7;
      p.setPen(QPen(lightFill ? QColor(0, 0, 0, 102) : QColor(255, 255, 255, 102), 1));
      p.setBrush(color);
      p.drawRoundedRect(QRectF(0.5, 0.5, 31.0, 15.0), 4, 4);
    }
    btn->setIcon(QIcon(pm));
    btn->setIconSize(pm.size());
  }

  // Load a local file as a fresh image, page-sized to the current page setting, clearing
  // any source/resource provenance. Notifies + refreshes actions. Returns whether it loaded.
  bool MainWindow::loadLocalImageReset(const QString& path) {
    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings_.customPageWidth,
                                              settings_.customPageHeight);
    canvas_->setPageCm(page.width, page.height);
    if (!canvas_->loadImage(path)) {
      notify_->error("Failed to load image");
      return false;
    }
    retainSourceFromFile(path);   // keep the untouched file bytes for a lossless .stencil bundle
    currentSource_.clear();  // a local file has no source/resource provenance
    currentResource_.clear();
    blankColor_.clear();     // a loaded image is not a blank project
    refreshActions();
    return true;
  }

  // File ▸ Open (the single top-left entry) opens the unified dialog in file/URL mode.
  void MainWindow::openImage() { openImageDialog(/*startBlank=*/false); }

  // The unified Open dialog (mirrors browser openImageModal.js): a local file, a web
  // URL/reference, or a NEW BLANK canvas. `startBlank` opens straight in blank mode
  // (the idle-canvas + projects "new blank" shortcuts). Replaces the former split of
  // Open Image / Open Another Image / New Blank Image. We dispatch on the chosen outcome.
  void MainWindow::openImageDialog(bool startBlank) {
    const auto px = core::defaultBlankSizePx(currentPageDimensions());
    OpenImageDialog dlg(this, canReplaceActive(), px.width, px.height, startBlank,
                        settings_.pageSize, settings_.units);
    if (execMaybePopover(dlg) != QDialog::Accepted) return;
    if (dlg.outcome() == OpenImageDialog::Outcome::Blank) {
      createBlankImageFromDialog(dlg.blankColor(), dlg.blankWidth(), dlg.blankHeight());
      return;
    }
    const QString src = dlg.source();
    if (src.isEmpty()) return;
    const OpenImageDialog::Outcome outcome = dlg.outcome();

    // Preview path: the dialog already decoded the exact image/frame and chose a
    // quick-crop. Adopt those pixels directly — no re-download/seek — and honor the
    // Crop toggle (on ⇒ crop centered to the chosen page/orientation; off ⇒ open the
    // whole image). Consumed exactly like openLinks() consumes LinksDialog. Applies to
    // the "open as new" outcomes (Here / new window); Replace keeps its own in-place
    // path below. Falls back to the async resolve when no preview was made.
    const QImage previewed = dlg.previewedImage();
    if (!previewed.isNull() &&
        (outcome == OpenImageDialog::Outcome::Here ||
         outcome == OpenImageDialog::Outcome::NewWindow)) {
      const bool localFile = !dlg.isUrl() && !dlg.isVideo() && QFileInfo(src).exists();
      if (outcome == OpenImageDialog::Outcome::NewWindow) {
        // The fresh window re-resolves the same source (identical pixels) and applies
        // the same page-aspect crop, so preview + crop carry across without moving pixels.
        openSourceInNewWindow(src, dlg.frame(), dlg.incognito(), /*hasPreview=*/true,
                              dlg.cropToPage(), dlg.cropAlbum(), dlg.cropPageSize());
        return;
      }
      openPreviewedImageHere(previewed, localFile ? src : QString(),
                             dlg.isUrl() ? src : QString(), dlg.incognito(),
                             dlg.cropToPage(), dlg.cropAlbum(), dlg.cropPageSize());
      return;
    }

    // No preview was taken (source typed but never previewed): keep the original async
    // resolve for a URL/video and the synchronous local-image load otherwise.
    if (dlg.isUrl() || dlg.isVideo()) {
      if (outcome == OpenImageDialog::Outcome::NewWindow)
        openSourceInNewWindow(src, dlg.frame(), dlg.incognito());
      else
        openSourceHere(src, dlg.frame(), dlg.incognito());
      return;
    }
    // A plain local image loads synchronously.
    if (outcome == OpenImageDialog::Outcome::NewWindow) {
      openImageInNewWindow(src, dlg.incognito());
    } else if (outcome == OpenImageDialog::Outcome::Replace) {
      replaceProjectImage(src, dlg.rename(), dlg.keepAnnotations());
    } else {
      openImageHere(src, dlg.incognito());
    }
  }

  // "Open here" for a URL / local video: mirror openImageHere's reset (persist the
  // current editor, drop the project binding, adopt the incognito choice) but load
  // via the async MediaLoader path. onLaunchImageLoaded then adopts a new project
  // (a no-op while incognito).
  void MainWindow::openSourceHere(const QString& src, int frame, bool incognito) {
    if (!incognito_) {
      if (!activeProjectId_.isEmpty()) saveToActiveProject();
      else saveSessionNow();
    }
    activeProjectId_.clear();
    if (incognito_ != incognito) {
      incognito_ = incognito;
      incognitoOverlay_->setActive(incognito);
      actIncognito_->blockSignals(true);
      actIncognito_->setChecked(incognito);
      actIncognito_->blockSignals(false);
      updateProjectTitle();
    }
    openImageSource(src, frame);  // async; failure is reported by MediaLoader
  }

  // "Open in new window" for a URL / local video: spawn a fresh window and hand it
  // the source via launch options (same vehicle as openImageInNewWindow, minus the
  // local-only QImageReader guard — MediaLoader validates + reports in that window).
  // A quick-crop override (from the Open-Image dialog's preview) rides along so the new
  // window applies the identical page-aspect crop after re-resolving the same source.
  void MainWindow::openSourceInNewWindow(const QString& src, int frame, bool incognito,
                                         bool hasPreview, bool cropToPage,
                                         bool cropAlbum, const QString& cropPage) {
    auto* win = new MainWindow(nullptr, /*restoreLast=*/false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    LaunchOptions opts;
    opts.src = src;
    opts.frame = frame;
    opts.incognito = incognito;
    // With a preview taken, carry the exact crop choice; crop OFF ⇒ open the whole
    // frame (skip the default page-aspect auto-crop), matching the "Open here" path.
    if (hasPreview) {
      opts.hasCropOverride = true;
      opts.cropToPage = cropToPage;
      opts.cropAlbum = cropAlbum;
      opts.cropPage = cropPage;
    }
    win->applyLaunchOptions(opts);
  }

  // Adopt the pixels the Open-Image dialog already decoded for its preview (no second
  // download/seek), honoring its quick-crop. Mirrors openSourceHere's editor reset
  // (persist the current editor, drop the project binding, adopt the incognito choice),
  // then routes the in-memory image through the shared onLaunchImageLoaded adoption —
  // exactly as openLinks() does with LinksDialog's previewed image.
  void MainWindow::openPreviewedImageHere(const QImage& image, const QString& localPath,
                                          const QString& provSource, bool incognito,
                                          bool cropToPage, bool cropAlbum,
                                          const QString& cropPage) {
    if (!incognito_) {
      if (!activeProjectId_.isEmpty()) saveToActiveProject();
      else saveSessionNow();
    }
    activeProjectId_.clear();
    if (incognito_ != incognito) {
      incognito_ = incognito;
      incognitoOverlay_->setActive(incognito);
      actIncognito_->blockSignals(true);
      actIncognito_->setChecked(incognito);
      actIncognito_->blockSignals(false);
      updateProjectTitle();
    }
    // Crop choice: page-aspect crop, or the whole frame (crop off) — never the default
    // page-aspect auto-crop, so what was previewed is what opens.
    if (cropToPage)
      pendingCrop_ = {QuickCropOpts::Mode::Page, cropAlbum, cropPage};
    else
      pendingCrop_ = {QuickCropOpts::Mode::None, false, QString()};
    pendingProvSource_ = provSource;
    onLaunchImageLoaded(image, localPath);
  }

  // True when the current editor holds a saved/linked project whose image can be swapped in
  // place (not a blank or incognito session — there's nothing to keep the same).
  bool MainWindow::canReplaceActive() const {
    return canvas_->hasImage() && !incognito_
        && (!activeProjectId_.isEmpty() || !remoteSession_->link().address.isEmpty());
  }

  // Replace the CURRENT project's image in place (same local id / server link), instead of
  // making a new project. `rename` adopts the new file's name; `keepAnnotations` keeps the
  // existing lines over the new image. Server sessions also re-upload the `original`.
  void MainWindow::replaceProjectImage(const QString& path, bool rename, bool keepAnnotations) {
    const core::Lines kept = keepAnnotations ? canvas_->allLines() : core::Lines{};
    if (!loadLocalImageReset(path)) return;   // loadImage clears lines + provenance, keeps binding
    if (keepAnnotations && !kept.empty()) canvas_->setLines(kept);
    if (rename) {
      const QString newName = QFileInfo(path).completeBaseName();
      if (!remoteSession_->link().address.isEmpty()) {
        remoteSession_->link().name = newName;
      } else if (Project* pr = findProject(activeProjectId_.toStdString())) {
        pr->meta.name = newName.toStdString();
      }
      updateProjectTitle();
    }
    // Server-linked: re-upload the new original (saveToServer only pushes the result), THEN
    // saveToActiveProject pushes the layout + rendered result. Ordering preserved: the original
    // upload + version refresh must finish before saveToActiveProject (whose guard reads it).
    QPointer<MainWindow> self(this);
    auto save = [this, self]() { if (self) saveToActiveProject(); };
    if (!remoteSession_->link().address.isEmpty())
      replaceServerOriginal(save);
    else
      save();
  }

  // Re-upload the linked server project's `original` with the current canvas image, refreshing
  // the version guard, then invoke `done`. No-op (but `done` still fires) when not server-linked
  // or sync is off (matches edit-in-memory).
  void MainWindow::replaceServerOriginal(std::function<void()> done) {
    if (remoteSession_->link().address.isEmpty() || !settings_.syncToServer) {
      if (done) done();
      return;
    }
    stencil::net::ServerClient* c = connections_ ? connections_->find(remoteSession_->link().address) : nullptr;
    if (!c || !canvas_->hasImage()) {
      if (done) done();
      return;
    }
    const int w = canvas_->imageWidth();
    const int h = canvas_->imageHeight();
    QPointer<MainWindow> self(this);
    c->uploadFileAsync(remoteSession_->link().id, "original", pngBytes(canvas_->image()), "png", w, h,
                       [this, self, c, done](bool uok) {
                         if (!self) return;
                         if (!uok) { if (done) done(); return; }
                         c->getProjectAsync(remoteSession_->link().id,
                                            [this, self, done](bool gok, stencil::net::ServerProject meta,
                                                               QJsonObject) {
                                              if (!self) return;
                                              if (gok) remoteSession_->link().version = meta.version;
                                              if (done) done();
                                            });
                       });
  }

  // Leave incognito and keep what is on screen as a LOCAL project (the local twin of
  // publishIncognitoToServer). Incognito's promise is that the app writes nothing on its own —
  // an explicit "save this" from the user is not the app deciding, so it is honoured here
  // instead of being refused.
  QString MainWindow::promoteIncognitoToLocal(const QString& name) {
    if (!canvas_->hasImage()) return QString();
    if (incognito_) {
      incognito_ = false;
      incognitoOverlay_->setActive(false);
      actIncognito_->blockSignals(true);
      actIncognito_->setChecked(false);
      actIncognito_->blockSignals(false);
      updateProjectTitle();
    }
    QString seed = name.trimmed();
    if (seed.isEmpty()) seed = canvas_->imageBaseName();
    if (seed.isEmpty()) seed = QStringLiteral("Untitled");
    const QString unique = uniqueLocalProjectName(seed);
    createLocalProject(unique, /*announce=*/false);
    return unique;
  }

  // Publish the current incognito session to a server: create the project there, upload the
  // original, link the session, leave incognito, then push the annotated layout + result.
  // Mirrors the browser's publishIncognitoToServer (a server-backed project is not incognito).
  void MainWindow::publishIncognitoToServer(const QString& serverUrl) {
    if (!canvas_->hasImage()) {
      notify_->error("Open an image first");
      return;
    }
    // Leave incognito first so the create/save paths persist normally.
    if (incognito_) {
      incognito_ = false;
      incognitoOverlay_->setActive(false);
      actIncognito_->blockSignals(true);
      actIncognito_->setChecked(false);
      actIncognito_->blockSignals(false);
    }
    QString name = canvas_->imageBaseName();
    if (name.isEmpty()) name = QStringLiteral("Untitled");
    QPointer<MainWindow> self(this);
    // create + upload original + link the session; the tail runs once linked (creation failure
    // notifies and never fires onLinked, so nothing is pushed — same as the old id-empty guard).
    createServerProject(serverUrl, name, [this, self]() {
      if (!self) return;
      // Push the annotated layout + result now, regardless of the sync toggle (explicit publish).
      // saveToServer reads settings_.syncToServer only at entry (synchronously), so restoring it
      // right after the async save is kicked off is safe.
      const bool savedSync = settings_.syncToServer;
      settings_.syncToServer = true;
      saveToServer();
      settings_.syncToServer = savedSync;
      remoteSync_->startRemotePoll();   // live co-edit: watch for peers changing this project
      refreshActions();
      updateProjectTitle();
    });
  }

  // Replace this editor's image with `path`. Mirrors the browser's openImageHere:
  // persist the current content first (unless incognito) so it isn't lost, then
  // start a fresh editor in the requested incognito mode and load the image.
  void MainWindow::openImageHere(const QString& path, bool incognito) {
    if (!incognito_) {
      if (!activeProjectId_.isEmpty()) saveToActiveProject();
      else saveSessionNow();
    }
    // Replacing the image wholesale resets the editor: drop the project binding and adopt
    // the chosen incognito mode directly (the toggle is normally gated to before an image).
    // Its signals are blocked so the toggle slot doesn't fire; we sync the title ourselves.
    activeProjectId_.clear();
    if (incognito_ != incognito) {
      incognito_ = incognito;
      incognitoOverlay_->setActive(incognito);
      actIncognito_->blockSignals(true);
      actIncognito_->setChecked(incognito);
      actIncognito_->blockSignals(false);
      updateProjectTitle();
    }
    if (loadLocalImageReset(path)) { playImageArrival(); adoptCanvasAsLocalProject(); }
  }

  // Launch `path` in a fresh, self-owned window, leaving this editor untouched
  // (the desktop analog of the browser's "open in new tab"). Reuses the launch
  // path (--src/--incognito), which honors incognito and the page-aspect crop.
  void MainWindow::openImageInNewWindow(const QString& path, bool incognito) {
    // The dialog only yields a local image file, and the new window's async launch path
    // can't report a load failure back here — so validate up front and show the error on
    // THIS window instead of spawning a blank one (mirrors openProjectInNewWindow's guard).
    if (!QImageReader(path).canRead()) {
      notify_->error("Failed to load image");
      return;
    }
    auto* win = new MainWindow(nullptr, /*restoreLast=*/false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    LaunchOptions opts;
    opts.src = path;
    opts.incognito = incognito;
    win->applyLaunchOptions(opts);
  }

  // Generate a solid-color image and adopt it exactly like a clipboard paste
  // (confirm-replace guard included), so editing/persistence behave as if the
  // image had been opened from disk. Mirrors browser blankImageModal.js.
  // The idle-canvas + projects "new blank" shortcuts open the unified Open dialog
  // straight in blank mode (blank creation is folded into the one Open dialog).
  void MainWindow::newBlankImage() { openImageDialog(/*startBlank=*/true); }

  // Generate a solid-color blank image from the unified dialog's blank mode and adopt
  // it (was the body of the retired standalone blank-image dialog flow).
  void MainWindow::createBlankImageFromDialog(const QColor& color, int w, int h) {
    if (canvas_->hasImage() &&
        QMessageBox::question(this, "Replace image",
                              "Replace the current image with a new blank image?")
            != QMessageBox::Yes) {
      return;
    }
    createBlankImage(color, w, h);
  }

  // The blank itself, with no confirmation: an op-plan already said what to do, and a
  // modal is something a plan cannot answer — it would stall the turn half-applied.
  void MainWindow::createBlankImage(const QColor& color, int w, int h) {
    // A blank's colour IS the page: a filter left over from the previous image
    // would repaint the fill (bw of a red page is flat gray), so start clean.
    if (settings_.imageFilter != QLatin1String("none")) applyImageFilter("none");
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    {
      const core::PageSize page = naturalPageCm(pageSizeValue(),
                                                settings_.customPageWidth,
                                                settings_.customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    activeProjectId_.clear();  // a new blank is a fresh editor, not the old project
    canvas_->loadFromImage(img);
    setSourceBytes({}, {});  // synthetic blank → re-encode from pixels on bundle
    currentSource_.clear();  // a generated blank image has no provenance
    currentResource_.clear();
    blankColor_ = color.name();  // mark this session as a (recolourable) blank of this fill
    canvas_->setBlankPage(true); // compare views keep a blank's fill + tint
    refreshActions();
    playImageArrival();   // a blank is an image appearing, so it assembles like any other
    notify_->success(QString("Blank %1×%2 image created").arg(w).arg(h));
    adoptCanvasAsLocalProject();  // persist so it appears in Projects (browser parity)
  }

  // Open the crop dialog over the ORIGINAL image and apply the chosen page-shaped
  // region. Mirrors browser cropModal.js: confirm before discarding lines when the
  // orientation flips; the original image is never replaced. Resizing within the
  // same orientation rescales the lines (the page relation is preserved).
  void MainWindow::openCropDialog() {
    if (!canvas_->hasImage()) {
      notify_->error("Open an image first");
      return;
    }
    const core::PageSize page = naturalPageCm(
        pageSizeValue(), settings_.customPageWidth, settings_.customPageHeight);
    canvas_->setPageCm(page.width, page.height);

    const core::CropRect cur = canvas_->cropRect();
    const bool album = core::isAlbumOrientation(cur.width, cur.height);
    // Preview the rotated original — cropRect lives in that pixel space.
    CropDialog dlg(canvas_->effectiveOriginalImage(), page.width, page.height, album, cur, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const core::CropRect next = dlg.cropRect();
    const core::CropChange ch = core::cropChange(cur, next);
    if (ch.orientationChanged && !canvas_->lines().empty() &&
        QMessageBox::question(
            this, "Change orientation",
            "Changing the crop orientation will remove all placed lines and "
            "points. Continue?") != QMessageBox::Yes) {
      return;
    }
    const bool hadLines = !canvas_->lines().empty();
    canvas_->applyCrop(next, /*recalc=*/true);
    fitToWindow();
    refreshActions();
    if (ch.orientationChanged && hadLines)
      notify_->success("Image cropped — lines removed (orientation changed)");
    else
      notify_->success("Image cropped");
  }

  // The canonical page-format value ("A4"/"custom") behind the combo's display
  // label — the item DATA, never the label text (which carries the physical
  // size in the display unit and, while searching, whatever the user typed).
  QString MainWindow::pageSizeValue() const {
    return pageSize_->currentData().toString();
  }

  // Re-render the page-format combo labels in the active display unit. Items,
  // data, and the selection are untouched, so no change handlers fire.
  void MainWindow::applyUnitToPageCombo() {
    if (!pageSize_) return;
    fillPageSizeCombo(pageSize_, /*includeCustom=*/true, settings_.units);
  }

  // Page dimensions for the current selection, honoring custom W x H (S10).
  core::PageSize MainWindow::currentPageDimensions() const {
    return core::pageDimensions(pageSizeValue().toStdString(),
                                canvas_->imageWidth(), canvas_->imageHeight(),
                                settings_.customPageWidth,
                                settings_.customPageHeight);
  }

  // Raw pixel -> page (cm), then the f(x)/f(y) formula transform (S11), exactly as
  // the browser composes pixelToPageCoords (drawingApp.js ~1756).
  core::Point MainWindow::pageCoords(double imageX, double imageY) const {
    const auto dims = currentPageDimensions();
    const auto raw = core::pixelToPageRaw(imageX, imageY, dims,
                                          canvas_->imageWidth(),
                                          canvas_->imageHeight());
    core::Point p;
    p.x = core::FormulaParser::apply(settings_.formulaX.toStdString(), 'x', raw.x,
                                     settings_.allowFormulas);
    p.y = core::FormulaParser::apply(settings_.formulaY.toStdString(), 'y', raw.y,
                                     settings_.allowFormulas);
    return p;
  }

  // Active display unit (cm by default; inches scales cm by 1/2.54). Shared with
  // the hover tooltip via core::buildTooltipRows.
  core::UnitFormat MainWindow::unitFormat() const {
    if (settings_.units == "in") return {1.0 / 2.54, "in"};
    return {1.0, "cm"};
  }

  // Total real-world length of every drawn line segment, in centimetres. Uses the raw
  // per-axis px→cm scale of pixelToPageRaw (NOT the formula/pageCoords path), so it is
  // independent of the display unit and of any coordinate formulas — mirroring
  // browser/js/core/units.js layoutLineLengthCm. Cached on the project meta at save
  // time to feed the projects-list tooltip cheaply. 0 when nothing is measurable.
  double MainWindow::currentLineLengthCm() const {
    const core::PageSize dims = currentPageDimensions();  // cm; already landscape-swaps
    const int cw = canvas_->imageWidth(), ch = canvas_->imageHeight();
    if (cw <= 0 || ch <= 0) return 0.0;
    const double sx = dims.width / cw, sy = dims.height / ch;
    const auto sumLine = [&](const core::Line& ln) {
      double t = 0.0;
      const auto& pts = ln.points;
      for (std::size_t i = 1; i < pts.size(); ++i)
        t += std::hypot((pts[i].x - pts[i - 1].x) * sx, (pts[i].y - pts[i - 1].y) * sy);
      return t;
    };
    // Sum committed lines by const-ref (no allLines() copy), then the in-progress line if any.
    double total = 0.0;
    for (const auto& ln : canvas_->lines()) total += sumLine(ln);
    if (!canvas_->currentLine().points.empty()) total += sumLine(canvas_->currentLine());
    return total;
  }

  // Stamp the display-only tooltip fields onto `meta` from the live canvas: image px
  // dimensions (0 when there is no image) and the total drawn-line length in cm.
  void MainWindow::stampCanvasMeta(core::ProjectMeta& meta) const {
    const bool hasImg = canvas_->hasImage();
    meta.imageW = hasImg ? canvas_->imageWidth() : 0;
    meta.imageH = hasImg ? canvas_->imageHeight() : 0;
    meta.lineLengthCm = currentLineLengthCm();
  }

  // Render the custom page spinboxes + their suffix label in the active unit.
  // Model values stay in cm; signals are blocked so the programmatic setValue
  // here doesn't feed back through the valueChanged handlers.
  void MainWindow::applyUnitToPageInputs() {
    if (!customW_ || !customH_) return;
    const auto u = unitFormat();
    const bool inches = (settings_.units == "in");
    QSignalBlocker bw(customW_), bh(customH_);
    customW_->setDecimals(inches ? 2 : 1);
    customH_->setDecimals(inches ? 2 : 1);
    customW_->setValue(settings_.customPageWidth * u.factor);
    customH_->setValue(settings_.customPageHeight * u.factor);
    if (customUnitLabel_)
      customUnitLabel_->setText(QString::fromStdString(u.label));
  }

  // Reflect settings_.units in both unit controls without firing their handlers.
  void MainWindow::syncUnitControls() {
    const bool inches = settings_.units == "in";
    if (actUnitCm_ && actUnitIn_) {
      QSignalBlocker bc(actUnitCm_), bi(actUnitIn_);
      actUnitIn_->setChecked(inches);
      actUnitCm_->setChecked(!inches);
    }
    if (unitCombo_) {
      QSignalBlocker b(unitCombo_);
      unitCombo_->setCurrentIndex(inches ? 1 : 0);
    }
  }

  // Change the active display unit from any surface (menu or toolbar combo):
  // persist, keep both controls in sync, and refresh every length readout.
  void MainWindow::applyUnits(const QString& code) {
    const QString c = (code == "in") ? "in" : "cm";
    if (settings_.units == c) return;
    settings_.units = c;
    persistSettings();
    syncUnitControls();
    applyUnitToPageCombo();  // page-format labels re-render in the new unit
    applyUnitToPageInputs();
    onHovered(lastHoverX_, lastHoverY_);  // status bar + live tooltip
    onSelectionChanged();                 // selection panel rows
  }

  // Reuse the core page metrics exactly as the browser's pixelToPageCoords does,
  // so the page readout matches between the two front-ends. Status mirrors the
  // browser status bar: Pixel / Page / To edge, in brackets, in the active unit.
  void MainWindow::onHovered(double imageX, double imageY) {
    if (!canvas_->hasImage()) {
      updateStatusIdle();   // nothing under the cursor to measure — clear, don't keep stale numbers
      return;
    }
    lastHoverX_ = imageX;
    lastHoverY_ = imageY;
    const auto page = pageCoords(imageX, imageY);
    const auto dims = currentPageDimensions();
    const auto u = unitFormat();
    const QString lbl = QString::fromStdString(u.label);
    status_->setText(
        QString("Pixel (%1, %2)     Page (%3, %4) %5     To edge (%6, %7) %5")
            .arg(qRound(imageX))
            .arg(qRound(imageY))
            .arg(page.x * u.factor, 0, 'f', 2)
            .arg(page.y * u.factor, 0, 'f', 2)
            .arg(lbl)
            .arg((dims.width - page.x) * u.factor, 0, 'f', 2)
            .arg((dims.height - page.y) * u.factor, 0, 'f', 2));
  }

  // Show/hide the custom inputs and recompute when the page size changes (S10).
  void MainWindow::onPageSizeChanged() {
    const bool custom = pageSizeValue() == "custom";
    if (customGroup_) customGroup_->setVisible(custom);
    settings_.pageSize = pageSizeValue();
    // Keep the canvas's default-crop aspect in sync with the selected page.
    {
      const core::PageSize page = naturalPageCm(pageSizeValue(),
                                                settings_.customPageWidth,
                                                settings_.customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    persistSettings();
    onHovered(lastHoverX_, lastHoverY_);
    onSelectionChanged();  // refresh panel cm for the new page size (GAP-2)
    remoteSync_->scheduleRemotePush();  // page format rides the layout — push to peers
  }

  // Validate fx/fy and apply them (S11; the browser's settingsController
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

  void MainWindow::refreshActions() {
    // A compare view is read-only — every annotation-editing action (and thus its keyboard
    // shortcut) is disabled while it's active. Only navigation + compare controls stay live.
    const bool ro = canvas_->compareReadOnly();
    actUndo_->setEnabled(canvas_->canUndo() && !ro);
    actRedo_->setEnabled(canvas_->canRedo() && !ro);
    actSaveProject_->setEnabled(!activeProjectId_.isEmpty());
    // Start only when an image is loaded and not already drawing; Stop only while
    // drawing (mirrors the browser HK_HANDLERS startDraw/stopDraw guards).
    const bool drawing = canvas_->isDrawing();
    actStartDraw_->setEnabled(canvas_->hasImage() && !drawing && !ro);
    actStopDraw_->setEnabled(drawing && !ro);
    // The toolbar shows ONE Draw button for both. Handing it the other action carries the
    // icon, tooltip, enabled state and click target across in one move — so it reads Stop
    // exactly while a session is live, and the two actions keep their own menu entries and
    // shortcuts (browser: DrawingApp.syncDrawToggleUI).
    if (startDrawBtn_) {
      // Pin the width to the wider of the two labels, once — otherwise "Start" → "Stop"
      // resizes the button and shifts the whole row (browser parity: .btn-draw-fixed).
      // Deferred to here because the themed icon and the stylesheet padding only exist
      // after the toolbar has been built and shown; measuring earlier comes out short.
      if (startDrawBtn_->maximumWidth() == QWIDGETSIZE_MAX && startDrawBtn_->isVisible() &&
          !startDrawBtn_->icon().isNull()) {
        QAction* keep = startDrawBtn_->defaultAction();
        int widest = 0;
        for (QAction* state : {actStartDraw_, actStopDraw_}) {
          startDrawBtn_->setDefaultAction(state);
          widest = std::max(widest, startDrawBtn_->sizeHint().width());
        }
        startDrawBtn_->setDefaultAction(keep);
        startDrawBtn_->setFixedWidth(widest);
      }
      QAction* want = drawing ? actStopDraw_ : actStartDraw_;
      if (startDrawBtn_->defaultAction() != want) startDrawBtn_->setDefaultAction(want);
    }
    // Accent-fill the Start button while a draw session is live (browser parity: start-drawing
    // gains the .active class). A dynamic property + repolish, so we don't make the action itself
    // checkable (which would add a stray check-mark to the Edit/context menus).
    if (startDrawBtn_ && startDrawBtn_->property("drawActive").toBool() != drawing) {
      startDrawBtn_->setProperty("drawActive", drawing);
      startDrawBtn_->style()->unpolish(startDrawBtn_);
      startDrawBtn_->style()->polish(startDrawBtn_);
    }
    // Its Draw-section neighbour, the Line/Rect toggle: same gate as the browser's
    // #draw-mode-toggle (drawingApp.js:2217 — needs an image, not while read-only), and the
    // same one-shot width pin as Start, so relabelling Line <-> Rect doesn't shift the row.
    if (drawModeBtn_) {
      drawModeBtn_->setEnabled(canvas_->hasImage() && !ro);
      if (drawModeBtn_->maximumWidth() == QWIDGETSIZE_MAX && drawModeBtn_->isVisible() &&
          !drawModeBtn_->icon().isNull()) {
        const QString keep = drawModeBtn_->text();
        int widest = 0;
        for (const char* t : {"Line", "Rect"}) {
          drawModeBtn_->setText(t);
          widest = std::max(widest, drawModeBtn_->sizeHint().width());
        }
        drawModeBtn_->setText(keep);
        drawModeBtn_->setFixedWidth(widest);
      }
    }
    // These are otherwise always enabled (they no-op internally when nothing applies);
    // the only gate is the read-only compare view.
    actNewLine_->setEnabled(!ro);
    actDeleteLast_->setEnabled(!ro);
    // …except Clear All Lines, which the browser greys with nothing to clear
    // (setDisabled('clear-all-lines', !hasLines || ro)) — and it is a loud red button.
    actClearAll_->setEnabled(!ro && !canvas_->allLines().empty());
    actDeleteLine_->setEnabled(!ro);
    actDeletePoint_->setEnabled(!ro);
    // Incognito can only be toggled before an image exists (S6).
    actIncognito_->setEnabled(!canvas_->hasImage());
    // Data actions (S9): layout export/copy need lines; importing a layout and
    // every image action need an image first (mirrors the browser guards). Paste
    // stays enabled so the Ctrl+V dispatch can still notify "Load an image first".
    const bool hasImg = canvas_->hasImage();
    const bool hasLines = !canvas_->allLines().empty();
    // Crop + the two rotations act on the loaded image, so grey them out without
    // one (parity with the browser's crop-image / rotate-left / rotate-right gating
    // in drawingApp.updateButtons — which gates on image presence only, since the
    // "Original" compare view still reflects crop + rotation, so no read-only gate).
    actCrop_->setEnabled(hasImg);
    actRotateLeft_->setEnabled(hasImg);
    actRotateRight_->setEnabled(hasImg);
    // Nothing to zoom without an image, so the whole ZOOM cluster goes dead — the two
    // step actions, the % field and Fit (browser: setDisabled over zoom-in / zoom-out /
    // zoom-fit / zoom-input). Alt+0 and the zoom shortcuts fall silent with them.
    actZoomIn_->setEnabled(hasImg);
    actZoomOut_->setEnabled(hasImg);
    actFit_->setEnabled(hasImg);
    if (zoom_) zoom_->setEnabled(hasImg);
    // The filter recolours the loaded image — nothing to apply it to without one
    // (browser: setDisabled('image-filter', !hasImage)). The tint swatch rides along.
    if (imageFilter_) imageFilter_->setEnabled(hasImg);
    if (filterColorBtn_) filterColorBtn_->setEnabled(hasImg);
    if (actCycleFilter_) actCycleFilter_->setEnabled(hasImg);
    // Save Session persists the whole blob (image, page, lines, filter, crop…), not
    // just the image — but restoreSession() ignores a session with no image AND no
    // lines, so saving in that state is a true no-op. Gate it on the same condition.
    actSaveSession_->setEnabled(hasImg || hasLines);
    actDownloadJson_->setEnabled(hasLines);
    actCopyLayout_->setEnabled(hasLines);
    actUploadJson_->setEnabled(hasImg);
    actSaveProjectFile_->setEnabled(hasImg);
    actPasteLayout_->setEnabled(hasImg);
    actSaveImage_->setEnabled(hasImg);
    actCopyImage_->setEnabled(hasImg);
    // IMAGE cluster empty state (browser #load-image-btn ↔ #image-actions): one labelled
    // Open button with no image, the per-image icon row once there is one. The BUTTONS are
    // toggled, never the actions — those also back menu entries, which must stay listed.
    if (openImageBtn_) openImageBtn_->setVisible(!hasImg);
    if (imageSection_) {
      for (QToolButton* b : imageSection_->findChildren<QToolButton*>()) {
        if (b == openImageBtn_) continue;
        b->setVisible(sectionButtonVisible(b->defaultAction(), b));
      }
    }
    // Compare view needs an image to compare against (parity with the browser gating).
    if (compareCombo_) compareCombo_->setEnabled(hasImg);
    if (actCycleCompare_) actCycleCompare_->setEnabled(hasImg);
    if (compareGroup_) compareGroup_->setEnabled(hasImg);
    // Image Links edits the CURRENT image's provenance — greyed out without one
    // (parity with the browser's disabled 🔗 button).
    if (actLinks_) actLinks_->setEnabled(hasImg);
    // "Open in…" mirrors the browser's #open-in-btn gating: hidden entirely when no
    // target is available (no browser URL, and no Telegram bot / not a server project),
    // otherwise enabled only with an image loaded.
    if (actOpenIn_) {
      const bool serverProj = !remoteSession_->link().address.isEmpty() && !remoteSession_->link().id.isEmpty();
      const bool browserAvail = !settings_.browserBaseUrl.trimmed().isEmpty();
      const bool telegramAvail = !settings_.telegramBotUsername.trimmed().isEmpty() && serverProj;
      const bool anyAvail = browserAvail || telegramAvail;
      actOpenIn_->setVisible(anyAvail);
      actOpenIn_->setEnabled(hasImg && anyAvail);
    }
    // Clear (remove) current project — mirrors the browser's updateButtons() gating
    // (clearBtn.style.display = remoteLink ? 'none' : ''): hidden whenever the current
    // session is server-linked (those are removed only from the projects dialog),
    // shown for local/temporary editors.
    if (actClearProject_) {
      actClearProject_->setVisible(remoteSession_->link().address.isEmpty());
      // No image ⇒ nothing to clear (browser: setDisabled('clear-storage', !hasImage)).
      // It used to stay enabled on an empty editor, where it only asked a question and
      // then "cleared" a canvas that was already empty.
      actClearProject_->setEnabled(canvas_->hasImage());
    }
    updateProjectTitle();   // keep the window title + toolbar name field in sync
    // Rename follows the name field itself: only a project that CAN be renamed offers it
    // (the field is disabled for no project / incognito), so the menu entry and the ✎ agree.
    if (actRenameProject_) actRenameProject_->setEnabled(projectName_ && projectName_->isEnabled());
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
    // and tooltip use, so formulas (S11) + custom page (S10) apply identically in
    // the panel. Mirrors browser/js/core/coordTable.js (px + cm per point).
    std::vector<QString> cmRows;
    if (line && canvas_->hasImage()) {
      const auto u = unitFormat();
      const QString ulbl = QString::fromStdString(u.label);
      cmRows.reserve(line->points.size());
      for (const auto& p : line->points) {
        const auto page = pageCoords(p.x, p.y);
        cmRows.push_back(QString("%1, %2 %3")
                             .arg(page.x * u.factor, 0, 'f', 2)
                             .arg(page.y * u.factor, 0, 'f', 2)
                             .arg(ulbl));
      }
    }
    // Points/coord table follows panelLine() (browser's always-on coordTable),
    // but the inline editor is gated on a real selection (selectedLine(), null
    // when selectedLineIdx_ < 0) so its mutators are never inert.
    selPanel_->showLine(line, hasImg ? canvas_->selectedLine() : nullptr,
                        hasImg ? canvas_->selectedPoint() : -1, cmRows);
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
  void MainWindow::showContextMenu(const QPoint& globalPos) {
    syncContextActions();

    // ── Build the menu tree. Order mirrors contextMenu.js inner() (~5-108):
    // Image/Layout · Fullscreen · Fit · — · Draw · DrawMode · DrawRect · — ·
    // Show Points/Lines · Clear · — · Style · Filter · Transformation · Tooltip.
    // StayOpenMenu keeps the menu open when a hosted checkbox/radio row is clicked (a plain QMenu
    // closes on release over a QWidgetAction). Submenus are StayOpenMenus too, for the same reason.
    StayOpenMenu menu(this);

    // Submenu-parent icons mirror contextMenu.js (folder / palette / image / function / message).
    // The menu is rebuilt per right-click, so the icons are (re)applied here in the current theme's
    // icon colour.
    const int subIcon = 18;
    auto subMenu = [&](const char* icon, const QString& title) -> StayOpenMenu* {
      auto* m = new StayOpenMenu(title, &menu);
      menu.addMenu(m)->setIcon(themedIcon(QString::fromLatin1(icon), iconColor_, subIcon));
      return m;
    };

    // Browser order: Fit FIRST (its most-reached-for entry), then Image/Layout,
    // then Fullscreen.
    menu.addAction(actFit_);

    // Image / Layout submenu (contextMenu.js:7-22).
    QMenu* layout = subMenu("folder", "Image / Layout");
    layout->addAction(actCopyImage_);
    layout->addAction(actPasteImage_);
    layout->addAction(actSaveImage_);  // "Download Image"
    layout->addSeparator();
    layout->addAction(actCopyLayout_);
    layout->addAction(actPasteLayout_);
    layout->addAction(actDownloadJson_);  // "Download Layout"
    layout->addAction(actUploadJson_);    // "Upload Layout"

    menu.addAction(actFullscreen_);
    menu.addSeparator();

    // Assistant submenu: a compact chat hosted in its own child menu, shown only
    // when a provider is configured (adds NO separator, so nothing dangles).
    // Live-input handling is scoped to THIS child menu — the root stays stock QMenu.
    if (settings_.llmProvider != QLatin1String("none")) {
      ensureChatMenuPanel();
      refreshLlmStatus();  // fresh provider dot/tooltip on the panel's gear
      StayOpenMenu* assistant = subMenu("sparkle", "Assistant");
      assistant->addAction(chatMenuAction_);
      assistant->setInteractiveArea(chatMenuPanel_, chatMenuInput_);
      // No separator BELOW it: the entry sits directly against the drawing
      // group (the separator above, after Fit, already opens the section). The
      // disabled case therefore leaves exactly the original separators.
    }

    // Drawing (contextMenu.js:28-31).
    menu.addAction(canvas_->isDrawing() ? actStopDraw_ : actStartDraw_);
    menu.addAction(actDrawModeToggle_);
    menu.addAction(actDrawRectNow_);
    menu.addSeparator();

    // Toggles + clear (contextMenu.js:33-36).
    menu.addAction(actShowPoints_);
    menu.addAction(actShowLines_);
    menu.addAction(actClearAll_);
    menu.addSeparator();

    // Style submenu (contextMenu.js:39-57).
    QMenu* style = subMenu("palette", "Style");
    style->addAction(pointSizeAction_);
    style->addAction(thicknessAction_);
    style->addSeparator();
    style->addAction(actStyleSolid_);
    style->addAction(actStyleDashed_);
    style->addAction(actStyleDotted_);

    // Image Filter submenu (contextMenu.js:59-74).
    // The \t column mirrors the browser's Alt+B badge on this parent row.
    QMenu* filter = subMenu("image", QStringLiteral("Image Filter\tAlt+B"));
    filter->addAction(actFilterNone_);
    filter->addAction(actFilterBW_);
    filter->addAction(actFilterSepia_);
    filter->addAction(actFilterInvert_);
    filter->addAction(actFilterContour_);
    filter->addAction(actFilterCustom_);
    filter->addSeparator();
    filter->addAction(tintColorAction_);

    // Transformation submenu (contextMenu.js:76-100): a "Coordinate Formulas" section with the
    // Allow Formulas checkbox and the x(x)/y(y) inputs (shown only while formulas are enabled).
    QMenu* transform = subMenu("function", "Transformation");
    transform->addSection("Coordinate Formulas");
    transform->addAction(ctxAllowFormulasAct_);
    transform->addAction(ctxFormulaXAct_);
    transform->addAction(ctxFormulaYAct_);

    // Tooltip submenu (contextMenu.js:96-107): enable toggle + the 3 row toggles, all
    // hosted QCheckBoxes so toggling one keeps the menu open (browser-parity live inputs).
    QMenu* tt = subMenu("message", "Tooltip");
    tt->addAction(actTooltipEnable_);
    tt->addSection("Show in Tooltip");   // browser parity: labelled header before the row toggles
    tt->addAction(actTtPage_);
    tt->addAction(actTtScreen_);
    tt->addAction(actTtCoords_);

    // No Deselect row — the browser menu ends at Tooltip (Esc still deselects).

    support::revealMenu(menu, globalPos);  // grow-from-the-cursor pop
    menu.exec(globalPos);
  }

  // Live-sync the persistent submenu state before exec (mirrors the browser
  // syncState() in contextMenu.js:239-297, which runs on open + on a timer).
  void MainWindow::syncContextActions() {
    const bool hasImg = canvas_->hasImage();
    const bool hasLines = !canvas_->allLines().empty();
    // Browser ctx-fs-label parity: the row names the direction it will take.
    actFullscreen_->setText(isFullScreen() ? QStringLiteral("Exit Fullscreen")
                                           : QStringLiteral("Enter Fullscreen"));
    // …and so does the tooltip: fullscreenLayer.js retitles the browser's button the same
    // way on every toggle, so a stale "Enter Fullscreen" is not what the hover should say.
    setActionTip(actFullscreen_, isFullScreen() ? "Exit fullscreen" : "Fullscreen mode");

    // Image / Layout enable-state (contextMenu.js:254-264).
    actCopyImage_->setEnabled(hasImg);
    actSaveImage_->setEnabled(hasImg);
    actPasteImage_->setEnabled(true);  // dispatch notifies "Load an image first"
    actCopyLayout_->setEnabled(hasLines);
    actDownloadJson_->setEnabled(hasLines);
    actPasteLayout_->setEnabled(hasImg);
    actUploadJson_->setEnabled(hasImg);
    actSaveProjectFile_->setEnabled(hasImg);

    // Draw-mode bridge label (contextMenu.js:249-252).
    const bool isRect = canvas_->drawMode() == CanvasWidget::DrawMode::Rect;
    actDrawModeToggle_->setText(isRect ? "Switch to Line Drawing"
                                       : "Switch to Rectangle Drawing");

    // Style submenu values (contextMenu.js:274-276). Block so seeding the
    // spinboxes/radios doesn't re-fire change handlers.
    {
      QSignalBlocker bm(pointSpin_), bt(thickSpin_);
      pointSpin_->setValue(settings_.defaultPointSize);
      thickSpin_->setValue(settings_.defaultThickness);
    }
    for (QAction* a : lineStyleGroup_->actions())
      a->setChecked(a->data().toString() == settings_.defaultStyle);

    // Image-filter submenu (contextMenu.js:278-282): check the active filter and
    // show the tint action only for custom.
    for (QAbstractButton* b : filterButtons_->buttons()) {
      QSignalBlocker bl(b);   // seeding the check state must not re-fire applyImageFilter
      b->setChecked(b->property("filterValue").toString() == settings_.imageFilter);
    }
    tintColorAction_->setVisible(settings_.imageFilter == "custom");

    // Tooltip enable toggle + rows (contextMenu.js:289-293). Refresh the hosted checkboxes so
    // their state is correct the moment the menu opens (blocked so seeding doesn't re-fire the
    // toggle handlers). actTooltip_ (the View-menu twin) is kept in sync by the enable handler.
    {
      QSignalBlocker be(tooltipEnableCheck_), bp(ttPageCheck_), bs(ttScreenCheck_), bc(ttCoordsCheck_);
      tooltipEnableCheck_->setChecked(settings_.tooltipEnabled);
      ttPageCheck_->setChecked(settings_.tooltipShowPage);
      ttScreenCheck_->setChecked(settings_.tooltipShowScreen);
      ttCoordsCheck_->setChecked(settings_.tooltipShowCoords);
    }

    // Transformation submenu (contextMenu.js:294-297): seed the formula twins from settings_ and
    // show the x/y inputs only while formulas are enabled (blocked so seeding doesn't re-apply).
    {
      QSignalBlocker ba(ctxAllowFormulas_), bx(ctxFormulaX_), by(ctxFormulaY_);
      ctxAllowFormulas_->setChecked(settings_.allowFormulas);
      ctxFormulaX_->setText(settings_.formulaX);
      ctxFormulaY_->setText(settings_.formulaY);
    }
    ctxFormulaXAct_->setVisible(settings_.allowFormulas);
    ctxFormulaYAct_->setVisible(settings_.allowFormulas);
  }

  // Build + show the hover tooltip (S12). Port of tooltip.js applyHover:
  //   Alt -> hide; Ctrl (no Shift) -> live cursor coords; else nearest point;
  //   else hovered line (Start/End, or all points with Shift); else hide.
  void MainWindow::onHoverDetail(double imageX, double imageY,
                                 const QPoint& globalPos,
                                 Qt::KeyboardModifiers mods) {
    if (!settings_.tooltipEnabled || !canvas_->hasImage()) {
      tooltip_->hide();
      return;
    }
    if (mods & Qt::AltModifier) {  // Alt held -> hide
      tooltip_->hide();
      return;
    }
    // Compare view: the layout is drawn only over the EDITED region, so nothing the
    // "before" half covers can be labelled — the user cannot see it there.
    const auto shown = [this](double x, double y) {
      return !canvas_->compareReadOnly() || canvas_->compareShowsEdited(x, y);
    };
    if (!shown(imageX, imageY)) {
      tooltip_->hide();
      return;
    }
    const double scale = canvas_->scale();
    const auto dims = currentPageDimensions();

    auto rowsForPoint = [&](double px, double py) {
      const auto page = pageCoords(px, py);
      // Per-row visibility from the context-menu Tooltip submenu (S11; mirrors
      // contextMenu.js tooltipShowScreen/Page/Coords -> tooltip.js show()).
      core::TooltipRowFlags flags;
      flags.showScreen = settings_.tooltipShowScreen;
      flags.showPage = settings_.tooltipShowPage;
      flags.showCoords = settings_.tooltipShowCoords;
      const auto coreRows =
          core::buildTooltipRows({px, py}, page, dims, flags, unitFormat());
      std::vector<std::pair<QString, QString>> out;
      for (const auto& r : coreRows)
        out.emplace_back(QString::fromStdString(r.first),
                         QString::fromStdString(r.second));
      return out;
    };

    // Ctrl (no Shift) -> live cursor coords.
    if ((mods & Qt::ControlModifier) && !(mods & Qt::ShiftModifier)) {
      tooltip_->setRows(rowsForPoint(imageX, imageY));
      tooltip_->showAt(globalPos);
      return;
    }

    const core::Lines all = canvas_->allLines();

    // Nearest point within (pointSize + 6)/scale image px.
    const core::Point* nearest = nullptr;
    double bestD = 1e18;
    for (const auto& line : all) {
      const double thresh = (line.pointSize + 6.0) / scale;
      for (const auto& p : line.points) {
        const double d = std::hypot(imageX - p.x, imageY - p.y);
        if (d <= thresh && d < bestD) {
          bestD = d;
          nearest = &p;
        }
      }
    }
    if (nearest) {
      // A point straddling the divider is hit from the edited side but sits on the
      // original one — label it only where it is actually drawn.
      if (!shown(nearest->x, nearest->y)) {
        tooltip_->hide();
        return;
      }
      tooltip_->setRows(rowsForPoint(nearest->x, nearest->y));
      tooltip_->showAt(globalPos);
      return;
    }

    // Hovered line within (thickness/2 + 5)/scale image px of any segment.
    const core::Line* hitLine = nullptr;
    for (const auto& line : all) {
      const double thresh = (line.thickness / 2.0 + 5.0) / scale;
      for (std::size_t i = 0; i + 1 < line.points.size(); ++i) {
        const double d = core::distToSegment(imageX, imageY, line.points[i],
                                             line.points[i + 1]);
        if (d <= thresh) {
          hitLine = &line;
          break;
        }
      }
      if (hitLine) break;
    }
    if (!hitLine || hitLine->points.empty()) {
      tooltip_->hide();
      return;
    }

    // Line tooltip: Start/End, or ALL points with Shift (tooltip.js showLine).
    const bool showAll = bool(mods & Qt::ShiftModifier);
    std::vector<std::pair<QString, QString>> rows;
    const auto u = unitFormat();
    const QString ulbl = QString::fromStdString(u.label);
    auto fmt = [&](const QString& label, const core::Point& p) {
      const auto page = pageCoords(p.x, p.y);
      rows.emplace_back(
          label, QString("%1, %2 px   %3, %4 %5")
                     .arg(qRound(p.x)).arg(qRound(p.y))
                     .arg(page.x * u.factor, 0, 'f', 2)
                     .arg(page.y * u.factor, 0, 'f', 2)
                     .arg(ulbl));
    };
    const auto& pts = hitLine->points;
    if (showAll || pts.size() <= 2) {
      for (std::size_t i = 0; i < pts.size(); ++i)
        fmt(QString::number(i + 1), pts[i]);
    } else {
      fmt("Start", pts.front());
      fmt("End", pts.back());
    }
    tooltip_->setRows(rows);
    tooltip_->showAt(globalPos);
  }

  // ── data actions (S9) ──
  // Layout/image export + clipboard IO (downloadLayout/uploadLayout/copyLayout/pasteLayout/
  // applyLayoutJson/saveImageFile/copyImageToClipboard) live in DataExportController
  // (dataExportController.hpp), constructed as dataExport_. pasteImage() stays here (it
  // creates a project) and delegates its JSON-text fallback to dataExport_->pasteLayout().

  // Single Ctrl+V dispatch: an image on the clipboard wins, else fall back to a
  // layout JSON text payload. Mirrors the browser paste listener priority
  // (image first, then JSON — drawingApp.js :563-591).
  void MainWindow::pasteImage() {
    const QClipboard* clip = QGuiApplication::clipboard();
    const QImage img = clip->image();
    if (!img.isNull()) {
      if (canvas_->hasImage() &&
          QMessageBox::question(this, "Replace image",
                                "Replace current image with the pasted image?")
              != QMessageBox::Yes) {
        notify_->info("Image paste canceled");  // drawingApp.js:568
        return;
      }
      activeProjectId_.clear();  // pasted image is a fresh editor (a new project)
      canvas_->loadFromImage(img);
      playImageArrival();
      setSourceBytes({}, {});  // clipboard pixels have no encoded source → re-encode on bundle
      currentSource_.clear();
      currentResource_.clear();
      refreshActions();
      notify_->success("Image pasted from clipboard");
      adoptCanvasAsLocalProject();
      return;
    }
    // No image — try a layout JSON text payload (drawingApp.js :582-591).
    dataExport_->pasteLayout();
  }

  // ── view / zoom ──
  void MainWindow::zoomStep(int dir) {
    setZoom(canvas_->scale() * (dir > 0 ? 1.25 : 0.8));
  }
  void MainWindow::zoomIn() { setZoom(canvas_->scale() * 1.25); }
  void MainWindow::zoomOut() { setZoom(canvas_->scale() * 0.8); }

  void MainWindow::setZoom(double scale, bool syncCombo) {
    scale = core::clampScale(scale);  // shared [kZoomMin, kZoomMax] bound (core/state/zoomPan)
    canvas_->setScale(scale);
    if (syncCombo) {
      const QString pct = QString::number(qRound(scale * 100)) + "%";
      // setEditText (with NoInsert) only updates the visible text — it never
      // appends list items, so Ctrl+wheel no longer accumulates entries. Block
      // signals so reflecting a programmatic zoom doesn't re-trigger setZoom.
      QSignalBlocker block(zoom_);
      zoom_->setEditText(pct);
    }
  }

  // ── Fullscreen enter/exit motion ────────────────────────────────────────────
  // The window itself can't be animated between normal and fullscreen, but the
  // thing the user is actually looking at can: the canvas starts at the size it
  // APPEARED to be against the old viewport and rides to its true size, so entering
  // reads as the canvas stretching out and leaving as it minimising back. It always
  // LANDS on the scale the user picked — the zoom is preserved, matching the browser.
  void MainWindow::beginFullscreenZoom() {
    if (fsZoomAnim_) { fsZoomAnim_->stop(); fsZoomAnim_->deleteLater(); fsZoomAnim_ = nullptr; }
    if (!canvas_ || !canvas_->hasImage() || !scroll_ || !scroll_->viewport()) {
      fsZoomFromViewport_ = QSize();
      return;
    }
    fsZoomFromViewport_ = scroll_->viewport()->size();
    fsZoomWaits_ = 0;
    QTimer::singleShot(0, this, [this] { startFullscreenZoom(); });
  }

  void MainWindow::startFullscreenZoom() {
    if (!fsZoomFromViewport_.isValid() || fsZoomFromViewport_.isEmpty()) return;
    if (!canvas_ || !canvas_->hasImage() || !scroll_ || !scroll_->viewport()) return;
    const QSize now = scroll_->viewport()->size();
    if (now.isEmpty()) return;
    // showFullScreen()/showNormal() resize asynchronously on some platforms — wait for
    // the new geometry rather than animating against the old one. Bounded, so a window
    // manager that never resizes (an offscreen test) simply skips the animation.
    if (now == fsZoomFromViewport_) {
      if (++fsZoomWaits_ > 12) return;
      QTimer::singleShot(16, this, [this] { startFullscreenZoom(); });
      return;
    }
    const double ratio = std::min(double(fsZoomFromViewport_.width()) / now.width(),
                                  double(fsZoomFromViewport_.height()) / now.height());
    fsZoomFromViewport_ = QSize();   // consumed
    if (ratio < 0.05 || ratio > 20.0 || std::abs(ratio - 1.0) < 0.01) return;
    const double target = canvas_->scale();
    const double start = core::clampScale(target * ratio);
    if (std::abs(start - target) < 1e-4) return;

    auto* anim = new QVariantAnimation(this);
    fsZoomAnim_ = anim;
    // Long and hard-eased-out, matching FLIP_MS / FLIP_EASING in browser/js/ui/motion.js:
    // most of the distance early, coasting into the landing.
    anim->setDuration(560);
    anim->setEasingCurve(QEasingCurve::OutQuint);
    anim->setStartValue(start);
    anim->setEndValue(target);
    connect(anim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { if (canvas_) canvas_->setScale(v.toDouble()); });
    connect(anim, &QVariantAnimation::finished, this, [this, target] {
      fsZoomAnim_ = nullptr;
      setZoom(target);   // land exactly on the user's zoom and resync the combo
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

  void MainWindow::fitToWindow() {
    if (!canvas_->hasImage()) return;
    const QSize vp = scroll_->viewport()->size();
    const double sx = double(vp.width()) / canvas_->imageWidth();
    const double sy = double(vp.height()) / canvas_->imageHeight();
    setZoom(std::min(sx, sy) * 0.95);
  }

  void MainWindow::setToolbarsVisible(bool on) {
    // Reset any leftover animated max-height (a mid-animation state) before showing/hiding.
    for (QToolBar* tb : findChildren<QToolBar*>()) { tb->setMaximumHeight(QWIDGETSIZE_MAX); tb->setVisible(on); }
    positionOverlayArrows();
  }

  // The top menu toggles from the "Controls" pill in the header row (kept in sync here). The panel
  // hides from its own header chevron and re-opens from a single floating chevron that sits flush to
  // the canvas' right edge — shown ONLY while the panel is hidden, so it's never a dead-end.
  void MainWindow::buildOverlayArrows() {
    panelReopenBtn_ = new QToolButton(this);
    panelReopenBtn_->setCursor(Qt::PointingHandCursor);
    panelReopenBtn_->setFocusPolicy(Qt::NoFocus);   // ditto: no focus halo over the canvas
    panelReopenBtn_->setFixedSize(kPanelToggleBox, kPanelToggleBox);   // same rounded square as the panel-header chevron
    panelReopenBtn_->setIconSize(QSize(kPanelToggleGlyph, kPanelToggleGlyph));
    panelReopenBtn_->setToolTip(QString("Show panel (%1)").arg(hotkey("togglePointsList", "Alt+X")));
    panelReopenBtn_->setStyleSheet(panelToggleQss());
    connect(panelReopenBtn_, &QToolButton::clicked, this,
            [this] { if (actPanel_) actPanel_->setChecked(true); });
    panelReopenBtn_->hide();
    spinControlsPill(false);   // seed the pill's angle from the current toolbar state
    updatePanelReopenButton();
  }

  // The toast stack hangs off the WINDOW's bottom-left, which puts the status bar's coord
  // readout under it — so it is told to clear whatever that bar currently occupies (nothing
  // in fullscreen, where the bar is hidden). Refreshed from positionOverlayArrows, which
  // already runs on every resize, theme change and toolbar fold.
  void MainWindow::syncToastInset() {
    // Late dock signals during ~MainWindow land after the layout (and statusBar) died.
    if (tearingDown_ || !notify_) return;
    const QWidget* bar = statusBar();
    int bottom = bar && bar->isVisible() ? bar->height() : 0;
    int left = 0;
    // A docked chat panel owns its corner: the stack moves beside (left dock) or
    // above (bottom dock) it instead of overlapping the composer.
    if (chatDock_ && chatDock_->isVisible() && !chatDock_->isFloating()) {
      const Qt::DockWidgetArea area = dockWidgetArea(chatDock_);
      if (area == Qt::LeftDockWidgetArea) left = chatDock_->width() + 8;
      else if (area == Qt::BottomDockWidgetArea) bottom += chatDock_->height() + 8;
    }
    notify_->setLeftInset(left);
    notify_->setBottomInset(bottom);
  }

  void MainWindow::positionOverlayArrows() {
    syncToastInset();
    if (controlsPill_) {
      // ONE glyph, turned: 0° is ↑ (rows shown), 180° is ↓. spinControlsPill drives the
      // angle, this only paints whatever it currently is (it also runs on theme flips).
      const QColor ic = palette().color(QPalette::WindowText);
      controlsPill_->setIcon(rotatedIcon("chevron-up", ic, kPillChevron, pillChevronDeg_));
    }
    updatePanelReopenButton();
  }

  // Browser parity: `#toggle-controls .ic` spins half a turn on the fold's curve rather
  // than blinking to the opposite chevron.
  void MainWindow::spinControlsPill(bool animate) {
    const qreal to = (actToolbars_ && !actToolbars_->isChecked()) ? 180.0 : 0.0;
    if (pillSpinAnim_) { pillSpinAnim_->stop(); pillSpinAnim_->deleteLater(); pillSpinAnim_ = nullptr; }
    if (!animate || qFuzzyCompare(pillChevronDeg_ + 1.0, to + 1.0)) {
      pillChevronDeg_ = to;
      positionOverlayArrows();
      return;
    }
    pillSpinAnim_ = startExtentSlide(
        this, qRound(pillChevronDeg_), qRound(to), kFoldMs,
        [this](int v) { pillChevronDeg_ = v; positionOverlayArrows(); },
        [this] { pillSpinAnim_ = nullptr; });
  }

  void MainWindow::positionPanelReopenButton() {
    if (!panelReopenBtn_ || !scroll_) return;
    // Top-RIGHT of the canvas, inset from the viewport's right edge so it sits to the LEFT of any
    // vertical scrollbar (viewport()->width() already excludes the scrollbar) — not centred, not
    // overlapping the scrollbar. A small top margin keeps it clear of the toolbar edge, and the
    // side inset stops it from sitting flush against the window edge.
    QWidget* vp = scroll_->viewport();
    const QPoint tr = vp->mapTo(this, QPoint(vp->width(), 0));
    panelReopenBtn_->move(tr.x() - panelReopenBtn_->width() - kPanelToggleInset,
                          tr.y() + kPanelToggleTop);
  }

  void MainWindow::updatePanelReopenButton() {
    if (!panelReopenBtn_) return;
    // Only when the panel is fully hidden and we're not in fullscreen (which edge-hover-reveals it).
    const bool showBtn = selPanel_ && !selPanel_->isVisible() && !fsActive_;
    panelReopenBtn_->setVisible(showBtn);
    if (showBtn) {
      // Back to 0° — any spin from the last click ended with the panel open, i.e. hidden.
      spinIcon(panelReopenBtn_, "chevron-left", palette().color(QPalette::WindowText),
               kPanelToggleGlyph, 0, 0, 0);
      positionPanelReopenButton();
      panelReopenBtn_->raise();
    }
  }

  // Show = slide the dock open to full width; hide = slide it to 0 then fully hide it (the canvas
  // fills the freed space) and reveal the floating right-edge re-open chevron. QMainWindow overrides
  // a dock's maximumWidth during its own layout passes, so we pin min==max (setFixedWidth) each frame
  // to force the width, then release the constraint at the end.
  void MainWindow::setPanelShown(bool show, bool animate) {
    if (!selPanel_) return;
    if (panelAnim_) { panelAnim_->stop(); panelAnim_->deleteLater(); panelAnim_ = nullptr; }
    const int full = panelRestoreWidth_ > 120 ? panelRestoreWidth_ : 320;
    if (!show && selPanel_->isVisible() && selPanel_->width() > 120)
      panelRestoreWidth_ = selPanel_->width();
    // The two chevrons are different buttons in different places, but they read as one
    // toggle: whichever is on screen turns half a revolution with the slide and lands on
    // the glyph the other one takes over with. Driven here, so Alt+X and the View menu
    // turn it too — not just a click on the chevron itself.
    const int spinMs = animate ? kFoldMs : 0;
    if (show) spinIcon(panelReopenBtn_, "chevron-left", palette().color(QPalette::WindowText),
                       kPanelToggleGlyph, 0, 180, spinMs);
    else selPanel_->spinCollapseChevron(0, 180, spinMs);
    auto finish = [this, show] {
      selPanel_->setMinimumWidth(0);
      selPanel_->setMaximumWidth(QWIDGETSIZE_MAX);
      if (!show) selPanel_->hide();
      panelAnim_ = nullptr;
      updatePanelReopenButton();
    };
    int from, to;
    if (show) { selPanel_->show(); selPanel_->setFixedWidth(0); from = 0; to = full; }
    else { from = selPanel_->width() > 0 ? selPanel_->width() : full; to = 0; }
    if (!animate) { selPanel_->setFixedWidth(to); finish(); return; }
    panelAnim_ = startExtentSlide(
        this, from, to, 280, [this](int v) { selPanel_->setFixedWidth(v); }, finish);
  }

  // ── chat dock slide (browser chat-panel parity) ──
  // The browser panel slides in from its dock edge on open (~0.34 s) and back
  // out on close (~0.26 s), ease-out. Same technique as setPanelShown: pin
  // min==max (setFixedWidth/Height) on every frame so QMainWindow's own layout
  // passes can't override the extent, then release the constraint at the end so
  // the dock stays user-resizable. The axis follows the dock area — width for
  // left/right, height for top/bottom.
  void MainWindow::stopChatAnim() {
    // An INTERRUPTED slide never runs its completion, so the "leaving" state has
    // to be released here too — left set, it silently refused every row menu.
    chatClosing_ = false;
    if (chatDock_) chatDock_->setClosing(false);
    if (!chatAnim_) return;
    chatAnim_->stop();
    chatAnim_->deleteLater();
    chatAnim_ = nullptr;
    if (!chatDock_) return;
    // Never leave the dock pinned: a stopped slide must hand back the natural
    // constraints, or the dock stays stuck at its mid-animation extent.
    chatDock_->setMinimumWidth(chatNaturalMin_.width());
    chatDock_->setMaximumWidth(QWIDGETSIZE_MAX);
    chatDock_->setMinimumHeight(chatNaturalMin_.height());
    chatDock_->setMaximumHeight(QWIDGETSIZE_MAX);
  }

  void MainWindow::setChatShown(bool show, bool animate) {
    if (!chatDock_) return;
    const bool wasVisible = chatDock_->isVisible();
    stopChatAnim();  // re-entrancy: a second toggle mid-slide wins outright
    // Opening clears both the "closing" state and the unread mark — the user is
    // looking at the conversation now.
    if (show) {
      chatClosing_ = false;
      chatDock_->setClosing(false);
      setChatUnread(false);
    }
    // A full open supersedes the icon-popover shape: re-dock to the area the
    // popover displaced instead of reopening the tiny float at its old spot
    // (browser chatPanel restoreFromCompact parity).
    if (show && chatDock_->isFloating() && chatCompactPopover_) {
      chatCompactPopover_ = false;
      addDockWidget(chatCompactPrevArea_, chatDock_);
      chatDock_->setFloating(false);
    }
    // Floating = its own window: there is no dock edge to slide from, and clamping it
    // would fight the tear-off geometry. It flies out of (and back into) the toolbar
    // icon instead — the same motion every dialog uses.
    if (!tearingDown_ && animate && chatDock_->isFloating() && show != wasVisible) {
      QWidget* icon = buttonForAction(actChat_);
      if (show) {
        chatDock_->show();
        support::revealWindow(*chatDock_, icon);
      } else {
        chatClosing_ = true;
        chatDock_->setClosing(true);
        support::dismissWindow(*chatDock_, icon);   // hides at once; the ghost flies
        chatClosing_ = false;
        chatDock_->setClosing(false);
      }
      return;
    }
    if (tearingDown_ || chatDock_->isFloating() || !animate) {
      chatDock_->setVisible(show);
      return;
    }
    const Qt::DockWidgetArea area = dockWidgetArea(chatDock_);
    const bool horiz = area != Qt::TopDockWidgetArea && area != Qt::BottomDockWidgetArea;
    const auto extent = [this, horiz] {
      return horiz ? chatDock_->width() : chatDock_->height();
    };
    const auto pin = [this, horiz](int v) {
      if (horiz) chatDock_->setFixedWidth(v);
      else chatDock_->setFixedHeight(v);
    };
    // Reopen at the extent the dock had when it was last dismissed.
    if (!show && wasVisible && extent() > 80) chatRestoreExtent_ = extent();
    const int full = chatRestoreExtent_ > 80 ? chatRestoreExtent_ : (horiz ? 345 : 320);
    // Interrupting a hide part-way: grow from where it actually is, so a fast
    // double-toggle never snaps back to 0 first.
    const int from = show ? (wasVisible && extent() < full ? extent() : 0) : extent();
    const int to = show ? full : 0;
    if (show) {
      chatDock_->show();
      pin(from);
    }
    // A dock mid-slide is already "away" as far as results go: it stays
    // isVisible() for the whole 260ms, and a turn landing in that window used to
    // find a surface that could not actually show it, so it said nothing at all.
    if (!show) { chatClosing_ = true; chatDock_->setClosing(true); }
    chatAnim_ = startExtentSlide(this, from, to,
                                 show ? 340 : 260,  // browser: 0.34s in, 0.26s out
                                 pin, [this, show] {
                                   stopChatAnim();  // releases the pinned constraints
                                   if (!show) chatDock_->hide();
                                   chatClosing_ = false;
                                   chatDock_->setClosing(false);
                                   // Opening the assistant puts the caret where you are about
                                   // to type (browser parity) — after the slide, so the focus
                                   // is not stolen back by the animation's layout work.
                                   if (show) chatDock_->focusInput();
                                 });
  }

  // The chat icon's popover shape (browser chatPanel.js openCompact parity): the
  // SAME dock — same conversation, same attachments — floated at its compact
  // tear-off size and pinned next to the icon by the shared popover placement.
  // Idempotent while already floating there; a docked/hidden chat is torn off.
  void MainWindow::openChatCompact(QWidget* anchor) {
    if (!chatDock_ || tearingDown_ || !anchor) return;
    stopChatAnim();  // a popover open mid-slide wins outright (setChatShown rule)
    // Swapping shapes is a popover swap like any other: a chat already on screen
    // in its FULL shape (docked, or a float the user tore off) LEAVES through the
    // animated path — sliding back into its edge, or flying into the icon — and
    // the compact one opens once that has played, one window at a time. Without
    // this the outgoing window simply vanished under setFloating() below.
    // Already pinned exactly where this gesture wants it: raise and focus, and
    // never re-play a flight for a window that does not move.
    if (chatCompactShowing() && chatDock_->geometry() == compactChatRect(anchor)) {
      chatDock_->raise();
      chatDock_->activateWindow();
      chatDock_->focusInput();
      return;
    }
    // Anything else on screen LEAVES first — including a compact float that has to
    // move (the user dragged it, or another icon anchors it now). Teleporting that
    // window read as "the chat vanished", which is the bug this branch exists for.
    if (chatDock_->isVisible()) {
      const int outMs = chatDock_->isFloating() ? kWindowDismissMs : kChatSlideOutMs;
      chatCompactPopover_ = false;   // it is leaving; the next open re-establishes it
      setChatShown(false, /*animate=*/true);
      QPointer<QWidget> pin(anchor);
      QTimer::singleShot(support::motionReduced() ? 0 : outMs, this, [this, pin] {
        if (pin) openChatCompactNow(pin);
      });
      return;
    }
    openChatCompactNow(anchor);
  }

  // Where the compact popover sits for `anchor` (global): the shared popover
  // placement at the dock's own tear-off size. Shared by the open and the
  // already-there check above, so the two can never disagree.
  QRect MainWindow::compactChatRect(QWidget* anchor) const {
    if (!chatDock_ || !anchor) return {};
    const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const QRect screen = anchor->screen()->availableGeometry();
    return support::popoverRect(anchorRect, chatDock_->floatingDefaultSize(), screen);
  }

  void MainWindow::openChatCompactNow(QWidget* anchor) {
    if (!chatDock_ || tearingDown_ || !anchor) return;
    stopChatAnim();   // with motion reduced the slide-out may still be pinned
    // Remember the docked layout this popover displaces, so a later full open
    // (toolbar single click / hotkey) restores it instead of the popover rect.
    if (!chatDock_->isFloating()) {
      const Qt::DockWidgetArea area = dockWidgetArea(chatDock_);
      if (area != Qt::NoDockWidgetArea) chatCompactPrevArea_ = area;
    }
    chatDock_->setFloating(true);
    chatDock_->setGeometry(compactChatRect(anchor));
    chatDock_->show();
    setChatUnread(false);   // opening any surface marks the news as seen
    // …and the incoming one flies OUT of the icon, the same motion every other
    // popover opens with (the caller above already returned for a window that is
    // staying put, so reaching here always means a real open).
    support::revealWindow(*chatDock_, buttonForAction(actChat_));
    chatDock_->raise();
    chatDock_->activateWindow();
    chatDock_->focusInput();
    chatCompactPopover_ = true;  // after setFloating: adoption hooks fired above
  }

  bool MainWindow::chatCompactShowing() const {
    return chatCompactPopover_ && chatDock_ && chatDock_->isFloating() &&
           chatDock_->isVisible();
  }

  // The "?" is the COLLAPSED state's readout: while the tool rows are up they already show
  // the image size, so it would just repeat them. Shown only with the rows hidden AND
  // something worth reading — an image open, or incognito on.
  void MainWindow::refreshStatusHintVisibility() {
    // The size line belongs to the tool rows: it goes with them, and the "?" takes over as
    // the place those facts can still be read. Two readouts of the same thing, one at a time.
    if (imageSizeInfo_) imageSizeInfo_->setVisible(toolbarsShown_);
    if (!statusHintAction_) return;
    const bool live = (canvas_ && canvas_->hasImage()) || incognito_;
    statusHintAction_->setVisible(live && !toolbarsShown_);
  }

  void MainWindow::setToolbarsShown(bool show, bool animate) {
    toolbarsShown_ = show;
    refreshStatusHintVisibility();
    // The header row (Controls pill + project name) always stays — collapse only the tool rows,
    // mirroring the browser where the header keeps the pill/title while the body hides.
    QList<QToolBar*> bars;
    for (QToolBar* b : findChildren<QToolBar*>())
      if (b != headerToolbar_) bars.append(b);
    if (bars.isEmpty()) return;
    spinControlsPill(animate);   // the pill's chevron turns with the rows
    if (!animate) {
      if (barsAnim_) { barsAnim_->stop(); barsAnim_->deleteLater(); barsAnim_ = nullptr; }
      for (QToolBar* b : bars) { b->setMinimumHeight(0); b->setMaximumHeight(QWIDGETSIZE_MAX); b->setVisible(show); }
      positionOverlayArrows();
      return;
    }
    animateBarsHeight(bars, show);
  }

  // Height slide shared by the pill collapse/expand and the fullscreen edge-hover reveal. Pure geometry
  // (setFixedHeight pins min==max each frame so QMainWindow's layout can't override it) — no opacity /
  // graphics effect, which is what keeps it flicker-free through QMainWindow's per-frame relayout.
  void MainWindow::animateBarsHeight(const QList<QToolBar*>& bars, bool show) {
    if (bars.isEmpty()) return;
    if (barsAnim_) { barsAnim_->stop(); barsAnim_->deleteLater(); barsAnim_ = nullptr; }
    auto release = [bars] {
      for (QToolBar* b : bars) { b->setMinimumHeight(0); b->setMaximumHeight(QWIDGETSIZE_MAX); }
    };
    // Natural height each bar should expand to (measure before we start clamping them).
    int full = 0;
    for (QToolBar* b : bars) full = std::max(full, b->sizeHint().height());
    if (full <= 0) full = 40;
    const int from = show ? 0 : (bars.first()->height() > 0 ? bars.first()->height() : full);
    const int to = show ? full : 0;
    if (show) for (QToolBar* b : bars) { b->setFixedHeight(0); b->show(); }
    barsAnim_ = startExtentSlide(
        this, from, to, 280,
        [bars, this](int v) {
          for (QToolBar* b : bars) b->setFixedHeight(v);  // pin min==max on every row
          positionOverlayArrows();
        },
        [this, bars, show, release] {
          release();
          if (!show) for (QToolBar* b : bars) b->hide();
          barsAnim_ = nullptr;
          positionOverlayArrows();
        });
  }

  void MainWindow::toggleFullscreen() {
    if (fsActive_) {
      // Exit: stop the hover poll and restore the top menu + points panel (right, as before).
      fsActive_ = false;
      if (fsHoverTimer_) fsHoverTimer_->stop();
      // Cancel any in-flight edge-hover slide and release the pinned toolbar heights so the restore
      // below starts from a clean state (a leftover animation / fixed height would fight it).
      if (barsAnim_) { barsAnim_->stop(); barsAnim_->deleteLater(); barsAnim_ = nullptr; }
      for (QToolBar* b : findChildren<QToolBar*>()) { b->setMinimumHeight(0); b->setMaximumHeight(QWIDGETSIZE_MAX); }
      fsBarsShown_ = false;
      fsPanelShown_ = false;
      beginFullscreenZoom();
      showNormal();
      if (menuBar()) menuBar()->setVisible(true);
      statusBar()->setVisible(true);   // restore the bottom coord readout
      // The header row (Controls pill + project name) ALWAYS returns — it's the only way to re-show
      // the tool rows, so it must never stay hidden. The tool rows restore to their pre-fullscreen
      // shown/collapsed state (setToolbarsShown keeps the header, unlike setToolbarsVisible).
      if (headerToolbar_) { headerToolbar_->setMaximumHeight(QWIDGETSIZE_MAX); headerToolbar_->setVisible(true); }
      setToolbarsShown(fsWasToolbars_, false);
      // Sync the toggle-action checks WITHOUT re-firing their (animated) toggled handlers, then
      // restore the panel to its pre-fullscreen expanded/collapsed(rail) state (non-animated).
      if (actToolbars_) { QSignalBlocker b(actToolbars_); actToolbars_->setChecked(fsWasToolbars_); }
      if (actPanel_) { QSignalBlocker b(actPanel_); actPanel_->setChecked(fsWasPanel_); }
      setPanelShown(fsWasPanel_, false);
      positionOverlayArrows();
    } else {
      // Enter: hide the top menu (all toolbars + menubar) AND the points panel so the canvas fills
      // the screen; a cursor poll re-reveals the toolbars when the cursor touches the TOP edge and
      // the points panel (kept on the RIGHT) when it touches the RIGHT edge — mirrors the browser.
      fsWasToolbars_ = actToolbars_ ? actToolbars_->isChecked() : true;
      fsWasPanel_ = actPanel_ ? actPanel_->isChecked() : true;   // was the panel expanded (vs rail)?
      // Capture the panel's real width BEFORE hiding it, so the edge-hover reveal slides to exactly
      // that width instead of setPanelShown's 320px default — which would overshoot the panel's natural
      // width and snap back at the end of the slide (a visible jump).
      if (selPanel_->isVisible() && selPanel_->width() > 120) panelRestoreWidth_ = selPanel_->width();
      setToolbarsVisible(false);
      if (menuBar()) menuBar()->setVisible(false);
      statusBar()->setVisible(false);   // hide the bottom bar so the canvas fills the screen
      if (selPanel_->isVisible() && selPanel_->width() > 120) panelRestoreWidth_ = selPanel_->width();
      selPanel_->setVisible(false);   // hidden in fullscreen; revealed on right-edge hover
      fsBarsShown_ = false;
      fsPanelShown_ = false;
      fsActive_ = true;
      beginFullscreenZoom();
      showFullScreen();
      setFocus(Qt::OtherFocusReason);   // help key events reach us for the Escape-exits path
      if (fsHoverTimer_) fsHoverTimer_->start(16);   // ~60Hz poll: reveal reacts immediately on hover
    }
    // Reflect the on/off state on the toolbar button (accent fill via QToolButton:checked).
    // Guarded so it never re-enters through a toggled slot.
    if (actFullscreen_) { QSignalBlocker b(actFullscreen_); actFullscreen_->setChecked(fsActive_); }
  }

  // Fullscreen edge-hover reveal: show the toolbars while the cursor is at/over the TOP band, and
  // the points panel while it's at/over the RIGHT edge; hide each once the cursor leaves. Hysteresis
  // (a wide "keep" zone once shown) stops flicker as the cursor moves onto the revealed widget.
  void MainWindow::fsHoverTick() {
    if (!fsActive_) return;
    const QPoint p = mapFromGlobal(QCursor::pos());
    const int w = width(), h = height();
    if (p.x() < 0 || p.y() < 0 || p.x() > w || p.y() > h) return;  // cursor outside the window
    // Top toolbars: slide them in (all rows incl. header — fullscreen hid them) when the cursor enters
    // the top band, keep them while it stays within the taller keep-zone. Drive off the tracked target
    // (fsBarsShown_), NOT live isVisible(): during an animated hide the bars stay visible until the
    // slide ends, so reading isVisible() here would restart the hide every 50ms tick (that's flicker).
    const int tbBand = 150;   // keep-zone once shown (approx combined toolbar-row height)
    // The reveal band (only checked while hidden). On macOS the very top strip is grabbed by the
    // auto-revealing system menu bar, so start the band LOWER (clear that chrome) and make it SHORTER —
    // a slim hot-zone just below the menu bar. Elsewhere the whole top edge is ours.
#ifdef Q_OS_MACOS
    const int revealTop = 26, revealBot = 60;   // clear the ~24px macOS menu bar; 34px band
#else
    const int revealTop = 0, revealBot = 120;
#endif
    const bool wantTb = fsBarsShown_ ? (p.y() < tbBand) : (p.y() > revealTop && p.y() < revealBot);
    if (wantTb != fsBarsShown_) {
      fsBarsShown_ = wantTb;
      animateBarsHeight(findChildren<QToolBar*>(), wantTb);   // reuse the pill's smooth height slide
    }

    // Right points panel: reveal it when the cursor hits the right edge (a generous 28px band, not
    // 5px which was near-impossible to hit). Once shown, KEEP it shown while the cursor stays in the
    // right third of the window — a wide keep-zone so dragging the dock splitter to RESIZE the panel
    // (setPanelShown's finish() releases the fixed width, so the splitter is draggable) doesn't stray
    // out of the zone and auto-hide the panel mid-drag.
    const int pnlKeep = std::max(w / 3, (selPanel_->isVisible() ? selPanel_->width() : 0) + 140);
    const bool wantPnl = fsPanelShown_ ? (p.x() > w - pnlKeep) : (p.x() > w - 28);
    if (wantPnl != fsPanelShown_) {
      fsPanelShown_ = wantPnl;
      setPanelShown(wantPnl, true);
    }
  }

  // ── precise scroll + anchored zoom (S3; core/zoomPan math) ──
  void MainWindow::scrollTo(int x, int y) {
    auto* hb = scroll_->horizontalScrollBar();
    auto* vb = scroll_->verticalScrollBar();
    hb->setValue(std::clamp(x, hb->minimum(), hb->maximum()));
    vb->setValue(std::clamp(y, vb->minimum(), vb->maximum()));
  }

  // Zoom toward a cursor position (viewport coords), keeping the image pixel under
  // the cursor fixed. Mirrors zoomPan.js zoomToward via core::anchoredZoom.
  void MainWindow::setZoomAnchored(double newScale,
                                   const QPoint& cursorInViewport) {
    if (!canvas_->hasImage()) {
      setZoom(newScale);
      return;
    }
    const double oldScale = canvas_->scale();
    const double sl = scroll_->horizontalScrollBar()->value();
    const double st = scroll_->verticalScrollBar()->value();
    const auto z = core::anchoredZoom(sl, st, cursorInViewport.x(),
                                      cursorInViewport.y(), oldScale, newScale);
    setZoom(z.scale);
    scrollTo(qRound(z.scrollLeft), qRound(z.scrollTop));
  }

  // Arrow keys pan the viewport (S7; drawingApp.js arrow-pan ~497): 7 px, or 22
  // with Shift. Alt/Ctrl/Meta+arrows are reserved (don't pan).
  void MainWindow::keyPressEvent(QKeyEvent* event) {
    const auto mods = event->modifiers();
    const int key = event->key();

    // Escape leaves fullscreen (browser parity) — restores the toolbars + panel.
    if (key == Qt::Key_Escape && fsActive_) { toggleFullscreen(); event->accept(); return; }

    // Track R held for the Alt+R+←/→ line-rotate chord (mirror of the browser #rHeld).
    if (key == Qt::Key_R) { rKeyHeld_ = true; QMainWindow::keyPressEvent(event); return; }

    // Alt+Shift+O — momentary "peek at the original" (mirror of the browser hold). Handled
    // here rather than as a QAction because it needs key-up; auto-repeat is ignored.
    if (key == Qt::Key_O && (mods & Qt::AltModifier) && (mods & Qt::ShiftModifier) &&
        !(mods & (Qt::ControlModifier | Qt::MetaModifier))) {
      if (!event->isAutoRepeat() && canvas_->hasImage()) {
        canvas_->setCompareHoldOriginal(true);
        refreshActions();   // read-only peek greys the editing actions too (parity with browser)
      }
      event->accept();
      return;
    }

    // Alt+R + ←/→ → rotate the selected line(s) (← CCW, → CW), 3°/press. Mirrors the browser
    // chord and the Ctrl+Shift+wheel rotate. Only fires when something is selected.
    if ((mods & Qt::AltModifier) && rKeyHeld_ && canvas_->selectionCount() >= 1 &&
        !canvas_->compareReadOnly() && (key == Qt::Key_Left || key == Qt::Key_Right)) {
      constexpr double kRotStep = 3.14159265358979323846 / 60.0;  // 3° (matches wheel rotate)
      canvas_->rotateSelectedLine((key == Qt::Key_Left ? -kRotStep : kRotStep));
      event->accept();
      return;
    }

    // Alt+Shift + arrow → flip / rotate-90 the selected line(s) about the selection's
    // bounding-box centre (browser parity: ↑ flip horizontal, ↓ flip vertical,
    // → rotate +90°, ← rotate −90°). Rotate-90 reuses the arbitrary-angle rotate path.
    // Only fires with a selection and outside a read-only compare view (mirrors Alt+R).
    if ((mods & Qt::AltModifier) && (mods & Qt::ShiftModifier) &&
        !(mods & (Qt::ControlModifier | Qt::MetaModifier)) &&
        canvas_->selectionCount() >= 1 && !canvas_->compareReadOnly() &&
        (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left ||
         key == Qt::Key_Right)) {
      constexpr double kQuarterTurn = 3.14159265358979323846 / 2.0;  // ±90° about the centre
      if (key == Qt::Key_Up) canvas_->flipSelectedLine(true);
      else if (key == Qt::Key_Down) canvas_->flipSelectedLine(false);
      else if (key == Qt::Key_Right) canvas_->rotateSelectedLine(kQuarterTurn);
      else canvas_->rotateSelectedLine(-kQuarterTurn);  // Key_Left
      event->accept();
      return;
    }

    // Other Alt/Ctrl/Meta+arrow combos stay reserved (e.g. zoom).
    if (mods & (Qt::AltModifier | Qt::ControlModifier | Qt::MetaModifier)) {
      QMainWindow::keyPressEvent(event);
      return;
    }

    int dirX = 0;
    int dirY = 0;
    if (key == Qt::Key_Left) dirX = -1;
    else if (key == Qt::Key_Right) dirX = 1;
    else if (key == Qt::Key_Up) dirY = -1;
    else if (key == Qt::Key_Down) dirY = 1;
    else { QMainWindow::keyPressEvent(event); return; }

    // With a line selected, arrows NUDGE the selection (1px, Shift = 10px, image space);
    // with nothing selected they pan the viewport (7/22px), as before. A read-only compare
    // view disables the nudge — arrows always pan.
    if (canvas_->selectionCount() >= 1 && !canvas_->compareReadOnly()) {
      const int nStep = (mods & Qt::ShiftModifier) ? 10 : 1;
      canvas_->nudgeSelected(dirX * nStep, dirY * nStep);
      event->accept();
      return;
    }
    const int panStep = (mods & Qt::ShiftModifier) ? 22 : 7;
    scrollTo(scroll_->horizontalScrollBar()->value() + dirX * panStep,
             scroll_->verticalScrollBar()->value() + dirY * panStep);
    event->accept();
  }

  // Clear the R-held flag when it (or focus) is released, so the Alt+R+←/→ chord doesn't stick.
  void MainWindow::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_R) rKeyHeld_ = false;
    // End the Alt+Shift+O peek when the letter or any required modifier lifts.
    if (!event->isAutoRepeat() && canvas_->compareHoldOriginal() &&
        (event->key() == Qt::Key_O || event->key() == Qt::Key_Alt ||
         event->key() == Qt::Key_Shift || event->key() == Qt::Key_Meta ||
         event->key() == Qt::Key_Control)) {
      canvas_->setCompareHoldOriginal(false);
      refreshActions();   // restore the editing actions on release (parity with browser)
    }
    QMainWindow::keyReleaseEvent(event);
  }

  // Should this section button be on screen? Its action's own visibility, except in the
  // IMAGE cluster, which is empty-state aware: with no image only the labelled Open button
  // shows (browser #load-image-btn ↔ #image-actions). Both the per-action mirror and
  // refreshActions go through here, so whichever runs last agrees.
  bool MainWindow::sectionButtonVisible(QAction* act, QToolButton* btn) const {
    if (act && !act->isVisible()) return false;
    if (imageSection_ && btn && btn != openImageBtn_ && imageSection_->isAncestorOf(btn))
      return canvas_ && canvas_->hasImage();
    return true;
  }

  // Where the next dialog should grow from. Every action records its own origin when it
  // fires: its visible toolbar icon, else the menu row that was clicked, else nothing.
  // Only a handful of actions used to do this, so a dialog opened from the menu bar or a
  // shortcut flew out of whichever icon had been used last — or from a box above the
  // dialog when none had. Clearing the anchor for a button-less action is the point.
  void MainWindow::bindRevealAnchors() {
    for (QAction* a : findChildren<QAction*>()) bindRevealAnchor(a);
  }

  // Bound the moment the action is CREATED, so this is its first triggered() slot and runs
  // before the handler. Bound later — after the handlers — it recorded the anchor only once
  // exec() had returned, i.e. after the dialog had come and gone, so every dialog flew out
  // of the icon used the time before (and the first one out of nowhere).
  void MainWindow::bindRevealAnchor(QAction* a) {
    if (!a || a->property("revealBound").toBool()) return;
    a->setProperty("revealBound", true);
    connect(a, &QAction::triggered, this, [this, a] {
      dialogAnchor_ = buttonForAction(a);   // resolved at trigger time; buttons come later
      dialogAnchorRect_ = (menuRowAction_ == a) ? menuRowRect_ : QRect();
    });
  }

  // The visible toolbar button that presents `act`, if any — used to anchor the theme
  // wipe at the icon the user actually pressed.
  QWidget* MainWindow::buttonForAction(QAction* act) const {
    if (!act) return nullptr;
    for (QToolButton* b : findChildren<QToolButton*>())
      if (b->defaultAction() == act && b->isVisible()) return b;
    return nullptr;
  }


  // One-shot first-show fade-in (browser appReveal counterpart). Ramps window
  // opacity — no per-child graphics effect, so the canvas paint path is untouched.
  void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (!firstShow_) return;
    firstShow_ = false;
    setWindowOpacity(0.0);
    auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(240);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
  }

  // Closing is IMMEDIATE on every path — ⌘Q / app-menu Quit / Dock Quit /
  // window ✕ / Alt+F4 / window-manager close — with no confirmation modal
  // (deliberate user decision); autosave/session persistence below preserves
  // the work regardless.
  void MainWindow::closeEvent(QCloseEvent* event) {
    // Persist the dock/toolbar layout (selection-panel area etc.). The chat
    // dock rides along in the blob but is reset to hidden/default on boot —
    // it is session-transient like the browser panel. Incognito never writes
    // (persistSettings gates it).
    settings_.windowState = QString::fromLatin1(saveState(kToolbarLayoutVersion).toBase64());
    persistSettings();
    QMainWindow::closeEvent(event);
  }

  // Ctrl+D sets an explicit light/dark and stops following the OS (browser
  // behavior: a manual toggle overrides the system preference).
  void MainWindow::toggleTheme() {
    // One flip at a time: a second press mid-wipe restyles the window under an overlay
    // holding the PREVIOUS snapshot, and the two palettes tear across each other. The
    // browser gets this from the View Transitions API (a new transition supersedes the
    // one in flight); here the press is simply dropped until the wipe has finished.
    if (themeSwapping()) return;
    settings_.themeMode = resolveDark(settings_.themeMode) ? "light" : "dark";
    applySettings(settings_, true);
    notify_->info(settings_.themeMode == "dark" ? "Dark theme" : "Light theme");
  }

  void MainWindow::applySettings(const Settings& s, bool persist) {
    settings_ = s;
    canvas_->setDefaults(s.defaultColor, s.defaultThickness, s.defaultPointSize,
                         s.defaultStyle, s.defaultPointColor);
    canvas_->setHoldDrawDelay(s.holdDrawDelay);
    {
      QSignalBlocker bp(actShowPoints_);
      QSignalBlocker bl(actShowLines_);
      QSignalBlocker bt(actTooltip_);
      actShowPoints_->setChecked(s.showPoints);
      actShowLines_->setChecked(s.showLines);
      actTooltip_->setChecked(s.tooltipEnabled);
    }
    syncUnitControls();
    applyUnitToPageCombo();  // page-format labels in the restored unit
    canvas_->setShowPoints(s.showPoints);
    canvas_->setShowLines(s.showLines);
    {
      QSignalBlocker b(pageSize_);
      const int idx = pageSize_->findData(s.pageSize);
      if (idx >= 0) pageSize_->setCurrentIndex(idx);
    }
    // Sync custom page-size inputs (S10) in the active display unit.
    if (customW_) {
      applyUnitToPageInputs();
      if (customGroup_) customGroup_->setVisible(s.pageSize == "custom");
    }
    // Sync formula controls (S11).
    if (allowFormulas_) {
      QSignalBlocker ba(allowFormulas_);
      QSignalBlocker bx(formulaX_);
      QSignalBlocker by(formulaY_);
      allowFormulas_->setChecked(s.allowFormulas);
      formulaX_->setText(s.formulaX);
      formulaY_->setText(s.formulaY);
      if (formulaGroup_) formulaGroup_->setVisible(s.allowFormulas);
      formulaError_->setVisible(false);
      if (actAllowFormulas_) {
        QSignalBlocker baf(actAllowFormulas_);
        actAllowFormulas_->setChecked(s.allowFormulas);
      }
    }
    // Seed the Style toolbar row (S8): line defaults, image filter + tint. All
    // under signal blockers so seeding doesn't re-trigger the change handlers /
    // re-persist. The filter is applied to the canvas once at the end.
    lineColorValue_ = QColor(s.defaultColor);
    filterColorValue_ = QColor(s.filterColor);
    // The chips themselves are painted by applyTheme() at the end of this function, not
    // here: their frame is palette-coloured, and `settings_ = s` above has ALREADY handed
    // them the new theme — so repainting them now bakes the new border into the snapshot
    // the wipe is about to take, and the pickers sit there light while the window around
    // them is still dark until the circle finally reaches them. Only the values change here.
    if (lineThickness_) {
      QSignalBlocker bt(lineThickness_);
      lineThickness_->setValue(qRound(s.defaultThickness));
    }
    if (pointSize_) {
      QSignalBlocker bm(pointSize_);
      pointSize_->setValue(qRound(s.defaultPointSize));
    }
    if (lineStyle_) {
      QSignalBlocker bs(lineStyle_);
      const int idx = lineStyle_->findData(s.defaultStyle);
      lineStyle_->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    if (imageFilter_) {
      QSignalBlocker bf(imageFilter_);
      const int idx = imageFilter_->findData(s.imageFilter);
      imageFilter_->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    if (filterColorBtn_) filterColorBtn_->setVisible(s.imageFilter == "custom");
    canvas_->setImageFilter(s.imageFilter, filterColorValue_);
    if (chatDock_) refreshLlmStatus();  // re-describe + re-probe the AI provider
    applyTheme();
    // S6: even an explicit Settings-dialog save is suppressed in incognito.
    if (persist && !incognito_) fileStore::saveSettings(settings_);
  }


  // The chat gear's dedicated dialog: only the provider/base URL/model/API key/
  // server rows (llm-contract.md §5). It writes the SAME settings keys
  // through the same applySettings path as the full dialog, so either route
  // ends up in the same file — the full Settings dialog keeps its AI-assistant
  // group for users who go that way.
  void MainWindow::openAssistantSettings() {
    AssistantSettingsDialog dlg(settings_, this);
    if (dlg.exec() == QDialog::Accepted) {
      applySettings(dlg.result(), true);
    }
  }

  void MainWindow::openSettings() {
    SettingsDialog dlg(settings_, this);
    // execMaybePopover, not exec(): every other dialog-opening icon grows its window out
    // of the icon (and answers dblclick/right-click with the compact anchored shape).
    // Visuals live in here, so this one was the odd one out.
    if (execMaybePopover(dlg, actSettings_) == QDialog::Accepted) {
      applySettings(dlg.result(), true);
    }
  }

  // ── persistence ──
  // Incognito gates every write of the incognito editor's OWN state (session,
  // settings, promotion, shortcut overrides — a deliberate desktop-only widening
  // of the browser rule) but never maintenance on OTHER saved projects.
  void MainWindow::scheduleAutosave() {
    if (incognito_) return;  // S6: no autosave timer while incognito
    if (settings_.autosave) autosaveTimer_->start(600);
  }

  void MainWindow::saveSessionNow() {
    if (incognito_) return;  // S6: skip session writes while incognito
    // Sync off + a fetched server project = edit-in-memory only: don't persist the
    // restore blob either (the session is "stored nowhere").
    if (!remoteSession_->link().address.isEmpty() && !settings_.syncToServer) return;
    Session s;
    s.imagePath = canvas_->imagePath();
    s.pageSize = pageSizeValue();
    s.scale = canvas_->scale();
    s.lines = canvas_->allLines();
    s.customPageWidth = settings_.customPageWidth;
    s.customPageHeight = settings_.customPageHeight;
    // Image filter / tint / draw mode ride along in the layout blob (S8; browser
    // storage.js:40-41,54). drawMode mirrors the canvas, the filter/tint mirror
    // Settings (Step 3 applies them to the canvas).
    s.imageFilter = settings_.imageFilter;
    s.filterColor = settings_.filterColor;
    s.drawMode =
        canvas_->drawMode() == CanvasWidget::DrawMode::Rect ? "rect" : "line";
    s.cropRect = canvas_->cropRect();
    s.rotationQuarters = canvas_->rotationQuarters();
    s.activeProjectId = activeProjectId_;
    fileStore::saveSession(s);
  }

  void MainWindow::restoreSession() {
    auto sess = fileStore::loadSession();
    if (!sess) return;
    if (sess->lines.empty() && sess->imagePath.isEmpty()) return;
    {
      const core::PageSize page = naturalPageCm(
          sess->pageSize, sess->customPageWidth, sess->customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    canvas_->restore(sess->imagePath, sess->lines, sess->scale, sess->cropRect,
                     sess->rotationQuarters);
    // Re-bind the restored canvas to its project (when it still exists), so removing
    // that project empties the editor instead of orphaning its picture on screen.
    if (!sess->activeProjectId.isEmpty()
        && findProject(sess->activeProjectId.toStdString())) {
      activeProjectId_ = sess->activeProjectId;
    }
    {
      QSignalBlocker b(pageSize_);
      const int idx = pageSize_->findData(sess->pageSize);
      if (idx >= 0) pageSize_->setCurrentIndex(idx);
    }
    // Apply the persisted filter / tint / draw mode to the canvas (S8; browser
    // storage.js restore ~410-421). Settings/UI are reseeded under blockers so
    // the controls reflect the restored session without re-persisting.
    settings_.imageFilter = sess->imageFilter;
    settings_.filterColor = sess->filterColor;
    filterColorValue_ = QColor(sess->filterColor);
    if (filterColorBtn_) updateColorSwatch(filterColorBtn_, filterColorValue_);
    if (imageFilter_) {
      QSignalBlocker bf(imageFilter_);
      const int idx = imageFilter_->findData(sess->imageFilter);
      imageFilter_->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    if (filterColorBtn_)
      filterColorBtn_->setVisible(sess->imageFilter == "custom");
    canvas_->setImageFilter(sess->imageFilter, filterColorValue_);
    canvas_->setDrawMode(sess->drawMode == "rect"
                             ? CanvasWidget::DrawMode::Rect
                             : CanvasWidget::DrawMode::Line);
    setZoom(sess->scale);
  }

  // ── server connections ──
  stencil::net::ConnectionManager* MainWindow::ensureConnections() {
    if (!connections_) {
      connections_ = new stencil::net::ConnectionManager(this);
      // Persist the live set on every change (connect / disconnect / reconnect) so
      // it survives relaunch — the desktop analogue of the browser connectionManager
      // onChange → saveServers.
      connect(connections_, &stencil::net::ConnectionManager::changed, this, [this] {
        stencil::net::connectionStore::saveServers(connections_->snapshot());
      });
      // Hand the manager to the session so RemoteSession::requireClient + the sync controller
      // resolve clients through it (it starts null until this lazy creation).
      remoteSession_->setConnections(connections_);
    }
    return connections_;
  }

  void MainWindow::autoConnectServers() {
    if (!stencil::net::connectionStore::getAutoConnect()) return;
    const QVector<stencil::net::SavedServer> saved =
        stencil::net::connectionStore::loadSavedServers();
    if (saved.isEmpty()) return;
    stencil::net::ConnectionManager* mgr = ensureConnections();
    int failed = 0;
    for (const auto& srv : saved) {
      QString err;
      // The saved kind rides along: a proven admin credential mints straight away
      // instead of spending a doomed /projects probe on it first.
      if (!mgr->connectTo(srv.url, srv.token, err,
                          stencil::net::ServerClient::kindFromTag(srv.kind)))
        ++failed;  // a dead server stays absent
    }
    if (failed > 0)
      notify_->info(QString("Couldn't reach %1 saved server%2")
                        .arg(failed)
                        .arg(failed == 1 ? "" : "s"));
    warnInsecureConnections();
  }

  void MainWindow::warnInsecureConnections() {
    if (!connections_) return;
    QStringList insecure;
    for (auto* c : connections_->clients())
      if (stencil::net::ServerClient::isInsecureRemote(c->base())) insecure << c->base();
    if (insecure.isEmpty()) return;
    notify_->error(
        QString("Insecure connection: %1 uses plaintext http — your access token and "
                "images are sent unencrypted. Use https on untrusted networks.")
            .arg(insecure.join(", ")));
  }

  // ── Alt-peek helpers ──
  // The Alt+hover peek: open `act`'s popover pinned to `btn` (browser popover.js
  // altHover parity). Closes a floating compact chat the glide is moving off;
  // never adopts the machine's own open window.
  void MainWindow::altPeekOpen(QToolButton* btn, QAction* act) {
    if (!btn || !act || !act->isEnabled() || activePopover_) return;
    // Gliding off an open COMPACT chat (peek, linger, or sticky popover) closes
    // it; a docked chat panel — or a float the user chose (tear-off, the title
    // bar's float button) — is never touched (chatCompactShowing).
    if (act != actChat_ && actChat_ && actChat_->isChecked() && chatCompactShowing()) {
      altPeekAction_.clear();
      actChat_->setChecked(false);
    }
    stopLingerPoll();
    if (popoverClickTimer_) popoverClickTimer_->stop();
    popoverPendingAction_.clear();
    // A chat already on screen: the gesture still means "show it compact HERE",
    // so it takes the same animated swap the right-click route does (it used to
    // return early, leaving the window where it was). Never adopted as a peek,
    // though — the Alt release must not close what the user opened deliberately.
    if (act == actChat_ && actChat_->isChecked()) {
      popoverAnchor_.clear();
      altPeekAction_.clear();
      openChatCompact(btn);
      return;
    }
    popoverAnchor_ = btn;
    altPeekAction_ = act;
    act->trigger();
    // A modal dialog blocks in exec() until it closes, so reaching here means
    // the peek is over — only the NON-blocking chat keeps its flag until the
    // Alt release (or a later deliberate gesture) consumes it.
    if (act != actChat_) altPeekAction_.clear();
  }

  // A LINGERING window (an engaged peek whose Alt was released): poll the cursor
  // and close it once the pointer is outside — unless a field inside holds typed
  // content, or the chat composer does (never yank a window mid-typing).
  void MainWindow::startLingerPoll() {
    if (!lingerPoll_) {
      lingerPoll_ = new QTimer(this);
      lingerPoll_->setInterval(120);
      connect(lingerPoll_, &QTimer::timeout, this, [this] {
        QWidget* w = activePopover_
            ? static_cast<QWidget*>(activePopover_.data())
            : ((chatDock_ && chatDock_->isFloating() && chatDock_->isVisible()) ? chatDock_ : nullptr);
        if (!w) { lingerPoll_->stop(); return; }
        // The popover is a child widget, so ask the overlay where it is on screen.
        const QRect box = activePopover_ ? popoverRectGlobal() : w->frameGeometry();
        if (box.contains(QCursor::pos())) return;
        if (typedContentInside(w)) return;
        if (w == chatDock_ && chatDock_->hasComposerText()) return;
        lingerPoll_->stop();
        if (activePopover_) dismissPopover();
        else if (actChat_ && actChat_->isChecked()) actChat_->setChecked(false);
      });
    }
    lingerPoll_->start();
  }
  void MainWindow::stopLingerPoll() { if (lingerPoll_) lingerPoll_->stop(); }

  // exec() a dialog — centred window, or (when a gesture armed popoverAnchor_) a
  // compact popover pinned to that icon; call sites read exec()'s return unchanged.
  // Outside-click rejects via the app filter, but a NESTED dialog is not "outside".
  // Close the open popover by fading the WINDOW out, then rejecting — the
  // shrink-into-icon ghost only shows after the window unmaps, so fading avoids
  // a close/come-back blink. activePopover_ is dropped up front (re-entrancy).
  void MainWindow::dismissPopover() {
    if (!activePopover_) return;
    // Whatever was watching it has nothing left to watch — and the linger poll would
    // otherwise spend the closing animation looking at the floating chat dock instead.
    stopLingerPoll();
    // reject() is the whole dismissal: execMaybePopover's finished() handler owns the
    // collapse animation, so every route out closes exactly the same way.
    activePopover_->reject();
  }

  // The popover overlay's motion, matching the app's dialog reveal (modalReveal.cpp).
  static constexpr int kPopoverOpenMs = 300;
  static constexpr int kPopoverCloseMs = 240;

  QRect MainWindow::popoverRectGlobal() const {
    if (popoverOverlay_)
      return QRect(popoverOverlay_->mapToGlobal(QPoint(0, 0)), popoverOverlay_->size());
    return activePopover_ ? activePopover_->frameGeometry() : QRect();
  }

  bool MainWindow::handlePopoverPress(QWidget* target, const QPoint& globalPos,
                                      Qt::MouseButton button) {
    if (!activePopover_) return false;
    // Inside the popover itself is not "outside" — and it lives INSIDE this window now,
    // so this test, not the window it belongs to, is what tells the two apart.
    if (popoverRectGlobal().contains(globalPos)) return false;
    if (target) {
      // Delivered press: a press in a NESTED dialog (a confirm, a native picker)
      // belongs to another window and is left alone, so flows launched from inside
      // the popover keep working.
      if (target->window() != this) return false;
    } else {
      // Polled press: the same exemption, decided by geometry — any other visible
      // top-level (a nested dialog, a menu) owns that click.
      for (QWidget* w : QApplication::topLevelWidgets())
        if (w != this && w != activePopover_.data() && w->isVisible() &&
            w->frameGeometry().contains(globalPos))
          return false;
    }
    // Gestures on the LOGO while its accent popover PEEKS (browser parity): a
    // RIGHT-press PROMOTES the same popover to sticky — clearing the peek marker is
    // all it takes, the Alt release then leaves it alone — and a LEFT-press is a
    // NO-OP (no accent cycle, no dismiss: the peek's visibility belongs to the hold
    // gesture). Both are consumed; every other press dismisses.
    const bool onLogo =
        logoBtn_ && (target ? target == logoBtn_
                            : QRect(logoBtn_->mapToGlobal(QPoint(0, 0)), logoBtn_->size())
                                  .contains(globalPos));
    if (onLogo && altPeekAction_.data() == actAccent_ &&
        activePopover_->objectName() == QLatin1String("accentPopover")) {
      if (button == Qt::RightButton) { altPeekAction_.clear(); return true; }
      if (button == Qt::LeftButton) return true;
    }
    dismissPopover();
    // The press travels on (it always did), but the icon it landed on must not
    // re-OPEN what this click just dismissed — a real popup swallows its closing
    // click, and with the popover non-modal that gesture now actually reaches the
    // button. Consumed by the logo's click-cycle and the popover icons' deferred
    // click; harmless if no such gesture follows.
    if (target && (target == logoBtn_ || popoverButtons_.contains(target)))
      popoverDismissClick_ = true;
    return false;
  }

  // One window at a time, driven from the keyboard: while a dialog is up, the shortcut that
  // opened it closes it again, and ANOTHER window's shortcut swaps to that window. A modal
  // dialog runs its own event loop, so the main window's QActions never fire there — the
  // dialog carries its own copies of those chords for as long as it is showing, and the
  // originals are parked (an active QAction with the same chord would be ambiguous when the
  // dialog is an in-window popover, and neither would fire).
  template <typename Actions>
  static void wireWindowSwitching(QDialog& dlg, const Actions& actions, QAction* opener) {
    for (QAction* a : actions) {
      if (!a || a->shortcut().isEmpty()) continue;
      auto* sc = new QShortcut(a->shortcut(), &dlg);
      sc->setContext(Qt::WidgetWithChildrenShortcut);
      QObject::connect(sc, &QShortcut::activated, &dlg, [&dlg, a, opener] {
        // The same window: just close it. A different one: close, then open that instead
        // once this dialog's event loop has actually unwound.
        if (a != opener) QTimer::singleShot(0, a, &QAction::trigger);
        dlg.reject();
      });
    }
  }

  int MainWindow::execMaybePopover(QDialog& dlg, QAction* opener) {
    wireWindowSwitching(dlg, popoverDialogActions_, opener);
    // Park the originals while the dialog owns those chords, and put them back after.
    QList<QPair<QAction*, Qt::ShortcutContext>> parked;
    for (QAction* a : popoverDialogActions_) {
      if (!a || a->shortcut().isEmpty()) continue;
      parked.append({a, a->shortcutContext()});
      a->setShortcutContext(Qt::WidgetShortcut);
    }
    const QScopeGuard restore([&] {
      for (const auto& [a, ctx] : parked) a->setShortcutContext(ctx);
    });
    QWidget* anchor = popoverAnchor_.data();
    popoverAnchor_.clear();
    if (!anchor) {
      // Ordinary centred window: it still grows out of the icon that opened it — or,
      // when that icon is hidden, out of the menu row that was clicked.
      support::revealDialog(dlg, dialogAnchor_.data(), dialogAnchorRect_);
      return dlg.exec();
    }
    // The popover is a CHILD WIDGET, never a window of its own: a small frameless
    // top-level simply does not animate on macOS. As a child, grow/shrink are
    // ordinary widget animations; the dialog keeps its content/result identity.
    const QSize cap(470, 590);
    dlg.setMinimumSize(0, 0);
    dlg.setMaximumSize(cap);
    const QSize want(qMin(dlg.sizeHint().width(), cap.width()),
                     qMin(dlg.sizeHint().height(), cap.height()));

    auto* overlay = new QWidget(this);
    overlay->setObjectName(QStringLiteral("popoverOverlay"));   // themed + found by tests
    overlay->setAutoFillBackground(true);
    dlg.setParent(overlay);
    dlg.setWindowFlags(Qt::Widget);   // a plain child now: no frame, no title, no window
    dlg.setGeometry(QRect(QPoint(0, 0), want));
    dlg.show();

    // Anchored beside the icon in WINDOW coordinates, and kept inside the window: the
    // same popoverRect placement as before, with this window standing in for the screen.
    const QRect anchorGlobal(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const QRect windowGlobal(mapToGlobal(QPoint(0, 0)), size());
    const QRect box(mapFromGlobal(support::popoverRect(anchorGlobal, want, windowGlobal)
                                      .topLeft()),
                    want);
    const QRect fromBox(mapFromGlobal(anchorGlobal.topLeft()), anchorGlobal.size());
    overlay->setGeometry(support::motionReduced() ? box : fromBox);
    overlay->raise();
    overlay->show();
    dlg.setFocus(Qt::PopupFocusReason);   // Escape and typing go to the popover
    // Grow out of the icon (the app's open motion + duration), inside the window.
    if (!support::motionReduced()) {
      auto* fx = new QGraphicsOpacityEffect(overlay);
      fx->setOpacity(0.0);
      overlay->setGraphicsEffect(fx);
      auto* grow = new QPropertyAnimation(overlay, "geometry", overlay);
      grow->setDuration(kPopoverOpenMs);
      grow->setStartValue(fromBox);
      grow->setEndValue(box);
      grow->setEasingCurve(QEasingCurve::OutCubic);
      auto* fade = new QPropertyAnimation(fx, "opacity", overlay);
      fade->setDuration(kPopoverOpenMs);
      fade->setStartValue(0.0);
      fade->setEndValue(1.0);
      grow->start(QAbstractAnimation::DeleteWhenStopped);
      fade->start(QAbstractAnimation::DeleteWhenStopped);
    }
    activePopover_ = &dlg;
    popoverOverlay_ = overlay;
    // Alt-GLIDE: while any popover shows and Alt is HELD, the cursor landing on a
    // DIFFERENT popover icon closes this dialog and opens that icon's peek. The
    // modal loop blocks Enter/hover events, so a poll watches the cursor.
    QTimer glide;
    glide.setInterval(80);
    connect(&glide, &QTimer::timeout, this, [this, anchor] {
      if (!activePopover_) return;
      // altHeldForTest_: the offscreen GUI test's stand-in for a physically held Alt
      // (QTest key events never reach the platform's modifier state).
      if (!(QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier) && !altHeldForTest_)
        return;
      for (auto it = popoverButtons_.cbegin(); it != popoverButtons_.cend(); ++it) {
        auto* b = static_cast<QToolButton*>(it.key());
        if (b == anchor || !b->isVisible() || !it.value()->isEnabled()) continue;
        // underMouse() as backup, same as the Alt KeyPress loop (and the test's mock).
        if (!(b->underMouse() || b->rect().contains(b->mapFromGlobal(QCursor::pos()))))
          continue;
        altPeekNextButton_ = b;
        altPeekNextAction_ = it.value();
        dismissPopover();
        break;
      }
    });
    glide.start();
    // A nested loop, not exec(): the caller still blocks here and still reads a
    // QDialog::DialogCode, so no call site changes — but there is no modal window to
    // block the app, and (now) no window at all.
    QPointer<QDialog> alive(&dlg);
    QPointer<QWidget> overlayAlive(overlay);
    QEventLoop loop;
    bool ended = false, closing = false;
    const auto end = [&ended, &loop] { ended = true; loop.quit(); };
    // ONE close path, whatever ended the turn — an outside press, Escape, a row click,
    // the Alt release, the glide. The dialog hides itself on its way to finished(), so
    // its picture is frozen into the overlay first; then the box collapses back into the
    // icon it grew from and the loop ends with it.
    connect(&dlg, &QDialog::finished, &loop, [&] {
      if (closing) return;   // a second reject during the collapse is a no-op
      closing = true;
      if (!overlayAlive || support::motionReduced()) return end();
      if (alive) {
        auto* frozen = new QLabel(overlayAlive);
        frozen->setPixmap(alive->grab());   // grab() renders a hidden widget
        frozen->setGeometry(alive->geometry());
        frozen->show();
      }
      auto* fx = qobject_cast<QGraphicsOpacityEffect*>(overlayAlive->graphicsEffect());
      if (!fx) {
        fx = new QGraphicsOpacityEffect(overlayAlive);
        overlayAlive->setGraphicsEffect(fx);
      }
      fx->setOpacity(1.0);
      auto* shrink = new QPropertyAnimation(overlayAlive, "geometry", overlayAlive);
      shrink->setDuration(kPopoverCloseMs);
      shrink->setStartValue(overlayAlive->geometry());
      shrink->setEndValue(fromBox);
      shrink->setEasingCurve(QEasingCurve::InCubic);
      auto* fade = new QPropertyAnimation(fx, "opacity", overlayAlive);
      fade->setDuration(kPopoverCloseMs);
      fade->setStartValue(1.0);
      fade->setEndValue(0.0);
      connect(shrink, &QAbstractAnimation::finished, &loop, end);
      shrink->start(QAbstractAnimation::DeleteWhenStopped);
      fade->start(QAbstractAnimation::DeleteWhenStopped);
    });
    connect(&dlg, &QObject::destroyed, &loop, end);
    connect(qApp, &QCoreApplication::aboutToQuit, &loop, end);
    if (!ended) loop.exec();
    glide.stop();
    activePopover_.clear();
    popoverOverlay_.clear();
    const int result = alive ? alive->result() : int(QDialog::Rejected);
    // Hand the dialog back to its caller — it is a stack object, so it must NOT be left
    // parented to the overlay we are about to delete.
    if (alive) {
      alive->hide();
      alive->setParent(nullptr);
    }
    if (overlayAlive) overlayAlive->deleteLater();
    // The glide picked the next icon: open its peek once this dialog unwinds.
    if (altPeekNextAction_) {
      QTimer::singleShot(0, this, [this] {
        QToolButton* b = altPeekNextButton_.data();
        QAction* a = altPeekNextAction_.data();
        altPeekNextButton_.clear();
        altPeekNextAction_.clear();
        altPeekOpen(b, a);
      });
    }
    return result;
  }

  void MainWindow::openConnections() {
    ConnectDialog dlg(ensureConnections(), this);
    execMaybePopover(dlg, actConnect_);
    warnInsecureConnections();  // the dialog may have added a plaintext-remote connection
  }

  // ── projects ──
  void MainWindow::openProjects() {
    // Expiry sweep (one week), mirroring the browser store.
    projectsStore_.clearAll();
    std::vector<core::ProjectMeta> metas;
    for (const auto& pr : projectList_) metas.push_back(pr.meta);
    projectsStore_.load(metas);
    const auto expired = projectsStore_.sweepExpired(nowMs());
    if (!expired.empty()) {
      projectList_.erase(
          std::remove_if(projectList_.begin(), projectList_.end(),
                         [&](const Project& p) {
                           return std::find(expired.begin(), expired.end(),
                                            p.meta.id) != expired.end();
                         }),
          projectList_.end());
      // Not gated by incognito: operates on other saved projects, not the
      // incognito editor's content (see S6 scope note above).
      fileStore::saveProjects(projectList_);
    }

    ProjectsDialog dlg(projectList_, nowMs(), connections_, buildProjectThumbs(),
                       unitFormat(), this);
    dlg.setDragZones(projectZones_);   // the main-window drag-out zone overlay (open/new-window/remove)
    // "Clear All (Local)" is handled WHILE the dialog is up: it confirms itself (over its
    // own window), we remove the projects, and it repaints the now-empty list. Closing the
    // window to ask, then leaving it closed, lost the user their place.
    connect(&dlg, &ProjectsDialog::clearAllRequested, this, [this, &dlg] {
      const int n = static_cast<int>(projectList_.size());
      const bool hadActive = !activeProjectId_.isEmpty();
      projectList_.clear();
      if (hadActive) resetToBlankEditor();   // the open one went with them
      fileStore::saveProjects(projectList_);
      refreshActions();
      refreshDockMenu();
      // The rows are already scattering (ProjectsDialog::scatterRows); emptying the list in
      // the same turn pulled them out from under their own dust and dropped "No projects
      // yet" in underneath it. Rebuild once the motes have landed (browser: beginRemoval).
      QPointer<ProjectsDialog> live(&dlg);
      QTimer::singleShot(DisintegrateOverlay::kMs, this, [this, live] {
        if (live) live->setProjects(projectList_);
      });
      notify_->success(QString("Cleared %1 local project(s)").arg(n));
    });
    // Single Delete / batch Remove: same stay-open pattern — the dialog confirmed and is
    // scattering the rows; remove here, then repaint the still-open list once the dust lands.
    connect(&dlg, &ProjectsDialog::removeRequested, this,
            [this, &dlg](const QVector<QPair<QString, QString>>& items) {
      QPointer<ProjectsDialog> live(&dlg);
      const bool single = items.size() == 1 && items.first().second.isEmpty();
      // Block removing a project that's open in another window (matches the browser's
      // "open in another tab" guard). Restore the scattered row right away.
      if (single && projectOpenInOtherWindow(items.first().first)) {
        notify_->error("That project is open in another window — close it there first");
        if (live) live->setProjects(projectList_);
        return;
      }
      for (const auto& pr : items) {
        if (pr.second.isEmpty()) {
          eraseLocalProject(pr.first);
        } else if (auto* c = connections_ ? connections_->find(pr.second) : nullptr) {
          c->deleteProjectAsync(pr.first, [](bool) {});  // fire-and-forget; list refresh is independent
        }
      }
      // Not gated by incognito: operates on other saved projects, not the
      // incognito editor's content (see S6 scope note above).
      fileStore::saveProjects(projectList_);
      refreshActions();
      refreshDockMenu();  // drop it from the Dock "recent" list
      if (single) notify_->info("Project deleted");
      // Rebuild once the motes have landed (see the Clear All note above).
      QTimer::singleShot(DisintegrateOverlay::kMs, this, [this, live] {
        if (live) live->setProjects(projectList_);
      });
    });
    if (execMaybePopover(dlg) != QDialog::Accepted) return;

    using Action = ProjectsDialog::Action;
    // Open CONFIRM lives here — AFTER the dialog closed — not inside the drag release (where a
    // QMessageBox got dismissed by that same release, so nothing opened and the row snapped back).
    // (Remove confirms INSIDE the dialog instead, deferred a turn for the same reason.)
    // The gesture decides whether to ask: a single click (or Return, or a
    // drag-out) confirms; a double click opens straight away.
    auto confirmOpen = [this, &dlg](const QString& id, bool newWindow) -> bool {
      if (!dlg.confirmRequested()) return true;
      const Project* pr = findProject(id.toStdString());
      const QString nm = pr ? support::shortName(QString::fromStdString(pr->meta.name))
                           : QStringLiteral("this project");
      const QString msg = newWindow
          ? QString("Open \"%1\" in a new window?").arg(nm)
          : QString("Open \"%1\"? Any unsaved changes in the current window will be replaced.").arg(nm);
      return QMessageBox::question(this, "Open project", msg,
                                   QMessageBox::Open | QMessageBox::Cancel,
                                   QMessageBox::Open) == QMessageBox::Open;
    };
    if (dlg.action() == Action::Open) {
      if (!confirmOpen(dlg.selectedId(), false)) return;
      loadProjectIntoCanvas(dlg.selectedId());
    } else if (dlg.action() == Action::OpenRemote) {
      if (!confirmOpen(dlg.selectedId(), false)) return;
      openServerProject(dlg.selectedServerUrl(), dlg.selectedId());
    } else if (dlg.action() == Action::OpenInNewWindow) {
      if (!confirmOpen(dlg.selectedId(), true)) return;
      openProjectInNewWindow(dlg.selectedId());
    } else if (dlg.action() == Action::MoveToServer) {
      // Can't move a project that's open in another window — it would vanish there.
      if (projectOpenInOtherWindow(dlg.selectedId())) {
        notify_->error("That project is open in another window — close it there first");
        return;
      }
      projectTransfer_->moveLocalProjectToServer(dlg.selectedServerUrl(), dlg.selectedId());
    } else if (dlg.action() == Action::CopyToServer) {
      projectTransfer_->copyLocalProjectToServer(dlg.selectedServerUrl(), dlg.selectedId(), dlg.newName());
    } else if (dlg.action() == Action::MoveToLocal) {
      // Move-to-local is allowed even if a peer/other client has the project open (the
      // server delete just ends their live link — they keep their in-memory copy).
      projectTransfer_->moveServerProjectToLocal(dlg.selectedServerUrl(), dlg.selectedId());
    } else if (dlg.action() == Action::MakeLocalCopy) {
      projectTransfer_->makeLocalCopyOfServerProject(dlg.selectedServerUrl(), dlg.selectedId(), dlg.newName());
    } else if (dlg.action() == Action::BatchMoveToServer) {
      for (const auto& pr : dlg.batchItems()) projectTransfer_->moveLocalProjectToServer(dlg.selectedServerUrl(), pr.first);
    } else if (dlg.action() == Action::BatchCopyToServer) {
      for (const auto& pr : dlg.batchItems()) projectTransfer_->copyLocalProjectToServer(dlg.selectedServerUrl(), pr.first, QString());
    } else if (dlg.action() == Action::BatchMoveToLocal) {
      for (const auto& pr : dlg.batchItems()) projectTransfer_->moveServerProjectToLocal(pr.second, pr.first);
    } else if (dlg.action() == Action::BatchCopyToLocal) {
      // Bulk copy without opening each (empty name → keeps the server project's name). Each import
      // is async; refresh + notify once the last one lands (count preserved as the item total).
      const auto items = dlg.batchItems();
      const int total = static_cast<int>(items.size());
      if (total == 0) {
        refreshActions();
        refreshDockMenu();
        notify_->success(QStringLiteral("Made 0 local copy(ies)"));
      } else {
        auto remaining = std::make_shared<int>(total);
        QPointer<MainWindow> self(this);
        for (const auto& pr : items) {
          projectTransfer_->importServerProjectToLocal(
              pr.second, pr.first, /*removeFromServer=*/false, QString(),
              [this, self, remaining, total](bool, QString) {
                if (--*remaining == 0 && self) {
                  refreshActions();
                  refreshDockMenu();
                  notify_->success(QString("Made %1 local copy(ies)").arg(total));
                }
              });
        }
      }
    } else if (dlg.action() == Action::SetColor) {
      // Set/clear a project's accent colour (local meta or server PUT), then repaint the active
      // name if it's the one that changed. Capture the dialog's selection by value — `dlg` is
      // destroyed when openProjects returns, before the async server PUT completes.
      const QString cid = dlg.selectedId();
      const QString csrv = dlg.selectedServerUrl();
      const QString ccol = dlg.selectedColor();
      QPointer<MainWindow> self(this);
      setProjectColorById(cid, csrv, ccol, [this, self, cid, csrv, ccol](bool ok) {
        if (!self || !ok) return;
        if (csrv.isEmpty() && activeProjectId_ == cid) {
          updateProjectTitle();
        } else if (!csrv.isEmpty() && remoteSession_->link().id == cid
                   && remoteSession_->link().address == csrv) {
          remoteSession_->link().color = normalizeProjectColor(ccol).value_or(QString());
          updateProjectTitle();
        }
      });
    } else if (dlg.action() == Action::Rename) {
      // The dialog already validated, but re-validate here so any rename path is safe.
      renameProjectById(dlg.selectedId(), dlg.newName());
    } else if (dlg.action() == Action::Expiration) {
      Project* pr = findProject(dlg.selectedId().toStdString());
      if (!pr) return;
      // Explicit expiration editor (period selector + calendar + keep-forever),
      // mirroring the browser expiration modal. Not gated by incognito: operates
      // on other saved projects, not the incognito editor's content.
      ExpirationDialog exp(QString::fromStdString(pr->meta.name), pr->meta.expiresAt,
                           QString::fromStdString(pr->meta.refreshPeriod),
                           pr->meta.autoRefresh, nowMs(), this);
      if (exp.exec() != QDialog::Accepted) return;
      pr->meta.expiresAt = exp.expiresAtMs();
      pr->meta.refreshPeriod = exp.refreshPeriod().toStdString();
      pr->meta.autoRefresh = exp.autoRefresh();
      fileStore::saveProjects(projectList_);
      notify_->success(pr->meta.expiresAt == 0
                           ? QString("\"%1\" is kept forever")
                                 .arg(support::shortName(QString::fromStdString(pr->meta.name)))
                           : QString("\"%1\" expiration updated")
                                 .arg(support::shortName(QString::fromStdString(pr->meta.name))));
    } else if (dlg.action() == Action::New) {
      if (incognito_) {  // an explicit promotion out of incognito, not an app-side write
        const QString promoted = promoteIncognitoToLocal(dlg.newName());
        notify_->success(promoted.isEmpty()
                             ? QStringLiteral("Nothing to save yet")
                             : QStringLiteral("Left incognito — saved \"%1\"")
                                   .arg(support::shortName(promoted)));
        return;
      }
      createProject(dlg.newName());
    } else if (dlg.action() == Action::NewBlank) {
      newBlankImage();
    }
  }

  // See buildProjectThumbs() in the header. The active project renders from the live
  // canvas (current, possibly unsaved edits); the rest composite offscreen from their
  // stored image+crop+rotation+lines. A pathless source has no pixels to reload.
  QHash<QString, QPixmap> MainWindow::buildProjectThumbs() const {
    QHash<QString, QPixmap> out;
    // Rendered larger than the 56px row icon so the dialog's hover-magnify preview
    // stays crisp; the list downscales it for the icon column via setIconSize.
    constexpr int kThumb = 320;
    const bool dark = resolveDark(settings_.themeMode);
    // One reusable offscreen renderer (never shown), themed + flagged to match the
    // editor so the previews look like what the user would see on open.
    CanvasWidget off;
    off.setDark(dark);
    off.setAccent(settings_.accentColor);
    off.setShowPoints(settings_.showPoints);
    off.setShowLines(settings_.showLines);
    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings_.customPageWidth,
                                              settings_.customPageHeight);
    off.setPageCm(page.width, page.height);
    for (const auto& pr : projectList_) {
      const QString id = QString::fromStdString(pr.meta.id);
      QImage rendered;
      if (id == activeProjectId_ && canvas_->hasImage()) {
        rendered = canvas_->renderToImage(/*withOverlay=*/true);  // live edited result
      } else if (!pr.imagePath.isEmpty()) {
        off.restore(pr.imagePath, pr.lines, 1.0, pr.cropRect, pr.rotationQuarters);
        // Local projects don't persist a per-project filter; the canvas applies the
        // global filter on open, so the preview uses it too (what you'd see on open).
        off.setImageFilter(settings_.imageFilter, filterColorValue_);
        rendered = off.renderToImage(/*withOverlay=*/true);
      }
      if (rendered.isNull()) continue;
      out.insert(id, QPixmap::fromImage(rendered.scaled(
                         kThumb, kThumb, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    return out;
  }

  bool MainWindow::loadProjectIntoCanvas(const QString& id) {
    Project* pr = findProject(id.toStdString());
    if (!pr) return false;
    {
      const core::PageSize page = naturalPageCm(pageSizeValue(),
                                                settings_.customPageWidth,
                                                settings_.customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    canvas_->restore(pr->imagePath, pr->lines, canvas_->scale(), pr->cropRect,
                     pr->rotationQuarters);
    // Auto-refresh on open: restart the expiry window when enabled (mirrors the
    // browser storage.loadProject snap). Keep-forever (expiresAt 0) is untouched.
    if (pr->meta.autoRefresh && pr->meta.expiresAt != 0) {
      pr->meta.expiresAt = core::ProjectsStore::addPeriod(nowMs(), pr->meta.refreshPeriod);
      fileStore::saveProjects(projectList_);
    }
    activeProjectId_ = id;
    remoteSession_->link().unbind();  // a local project is not server-linked
    remoteSync_->stopRemotePoll();   // no longer a server session
    currentSource_ = QString::fromStdString(pr->meta.source);
    currentResource_ = QString::fromStdString(pr->meta.resource);
    // Restore the blank-fill colour so the Blank control reappears for a reopened blank.
    blankColor_ = pr->meta.blank ? QString::fromStdString(pr->meta.blankColor) : QString();
    canvas_->setBlankPage(!blankColor_.isEmpty());
    // Chat persistence (§12): with saving on the conversation is project-scoped —
    // swap in this project's saved chat (an absent one = a fresh scope). With it
    // off, the session conversation survives switches (the pre-§12 behavior).
    if (settings_.saveChatsWithProject) restoreChatFromDoc(pr->chat);
    refreshActions();
    fitToWindow();   // fit the opened project to the window (matches the browser)
    notify_->success(
        QString("Opened \"%1\"").arg(support::shortName(QString::fromStdString(pr->meta.name))));
    return true;
  }

  // Open a server-stored project: fetch its record (name/version/layout) + the
  // original image bytes, load them onto this canvas, and link the session so a
  namespace {
    // A stable value key for a line, for dedup when merging two editors' layouts on a
    // save conflict (mirrors the browser's JSON-stringify dedup in mergeLines).
    QString lineKey(const core::Line& l) {
      // pointColor rides second, matching the browser's lineDedupeKey field order: two
      // lines alike but for their point colour are different lines, and an UNSET one keys
      // as "" so a server round-trip (which omits the field) still dedupes against the
      // local original.
      QString k = QString("%1|%2|%3|%4|%5|%6|%7")
                      .arg(QString::fromStdString(l.color))
                      .arg(QString::fromStdString(l.pointColor))
                      .arg(l.thickness).arg(l.pointSize)
                      .arg(QString::fromStdString(l.style))
                      .arg(l.locked ? 1 : 0)
                      .arg(QString::fromStdString(l.fillColor));
      for (const auto& p : l.points) k += QString(";%1,%2").arg(p.x).arg(p.y);
      return k;
    }


    // GET an http(s) URL's bytes (no auth) with a 10s timeout,
    // delivering them to `done` on the event loop (empty on any failure/timeout). `ctx` owns the
    // transient QNetworkAccessManager — if `ctx` is destroyed mid-fetch the nam dies with it, the
    // reply is severed, and `done` never runs on a dangling caller (a safe no-op).
    void fetchUrlBytesAsync(QObject* ctx, const QString& url,
                            std::function<void(QByteArray)> done) {
      const QUrl u(url);
      if (!u.isValid() || (u.scheme() != "http" && u.scheme() != "https")) {
        done(QByteArray());
        return;
      }
      auto* nam = new QNetworkAccessManager(ctx);
      QNetworkRequest req(u);
      req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
      QNetworkReply* reply = nam->get(req);
      auto* timeout = new QTimer(nam);
      timeout->setSingleShot(true);
      // Timeout aborts the reply, which fires finished() with an error → empty result.
      QObject::connect(timeout, &QTimer::timeout, reply, [reply] { reply->abort(); });
      QObject::connect(reply, &QNetworkReply::finished, nam,
                       [reply, nam, done = std::move(done)]() {
                         QByteArray out;
                         if (reply->error() == QNetworkReply::NoError) out = reply->readAll();
                         reply->deleteLater();
                         nam->deleteLater();
                         done(out);
                       });
      timeout->start(10000);
    }

    // Read a layout's saved filter/tint (legacy blackAndWhite → "bw"); an absent or empty
    // layout yields "none" + the default tint.
    void parseLayoutFilter(const QJsonObject& layout, const QString& defTint,
                           QString& filter, QString& tint) {
      filter = layout.value("imageFilter")
                   .toString(layout.value("blackAndWhite").toBool(false) ? "bw" : "none");
      tint = layout.value("filterColor").toString(defTint);
    }
  }  // namespace

  // Adopt a full layout envelope onto `img` (crop + rotation + filter + lines +
  // page/formulas). Shared by openServerProject and the inline browser→desktop
  // "Open in…" hand-off so both restore the exact session — not just the lines.
  void MainWindow::loadImageWithLayout(const QImage& img, const QJsonObject& layout,
                                       const QByteArray& sourceBytes, const QString& sourceExt) {
    // Retain the untouched source bytes for a lossless .stencil re-bundle (empty ⇒ re-encode).
    setSourceBytes(sourceBytes, sourceExt);
    // Adopt the page format + formulas before sizing the canvas page below.
    adoptServerLayoutMeta(layout);
    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings_.customPageWidth,
                                              settings_.customPageHeight);
    canvas_->setPageCm(page.width, page.height);
    // Restore geometry (rotation + crop) from the layout, then adopt the lines. Rotation
    // applies before the crop (the crop lives in rotated-original space); an empty/old
    // layout default-crops and stays un-rotated.
    int lw = 0, lh = 0;
    core::CropRect crop;
    int rot = 0;
    core::Lines lines = fileStore::parseLayoutJson(layout, lw, lh, &crop, &rot);
    canvas_->loadFromImage(img, crop, rot);
    if (!lines.empty()) canvas_->setLines(lines);
    // Restore the saved filter/tint (an empty layout resets to "none" + the default tint,
    // so a prior image's filter — or the desktop's default filter — doesn't bleed in).
    QString filter, tint;
    parseLayoutFilter(layout, settings_.filterColor, filter, tint);
    applyTintColor(QColor(tint));
    applyImageFilter(filter);
  }

  // later Save writes back. Mirrors the browser projectsModal openRemote(). Async: chains
  // getProject → downloadFile("original") → (on empty) fetchUrlBytes(source) → decode → adopt.
  void MainWindow::openServerProject(const QString& serverUrl, const QString& id, bool silent,
                                     bool link) {
    if (!connections_) return;
    stencil::net::ServerClient* c = remoteSession_->requireClient(serverUrl);
    if (!c) return;
    // Loading the canvas below emits changed() — guard so it isn't taken for a user edit and
    // pushed straight back (feedback loop). This flag now reflects async-in-flight state (there is
    // no nested loop): set true here, cleared automatically when the whole chain ends. The shared
    // clearer's destructor runs once the last pending continuation is gone (success, error, OR the
    // client/window destroyed mid-flight), so the flag can never stick true.
    remoteReloading_ = true;
    auto reloadGuard = std::shared_ptr<void>(nullptr, [self = QPointer<MainWindow>(this)](void*) {
      if (self) self->remoteReloading_ = false;
    });
    QPointer<MainWindow> self(this);
    c->getProjectAsync(id, [this, self, c, serverUrl, id, silent, link, reloadGuard](
                               bool ok, stencil::net::ServerProject meta, QJsonObject layout) {
      if (!self) return;
      if (!ok) {
        notify_->error(QString("Could not open server project — %1").arg(c->lastError()));
        return;
      }
      // Adopt the decoded bytes onto the canvas + link the session (the tail shared by the
      // stored-bytes and source-URL-fallback paths).
      auto adopt = [this, self, serverUrl, id, silent, link, meta, layout,
                    reloadGuard](QByteArray bytes) {
        if (!self) return;
        QImage img;
        if (!img.loadFromData(bytes)) {
          notify_->error("Server image could not be decoded");
          return;
        }
        // Adopt the full layout (page/formulas + geometry + lines + filter) onto the image.
        loadImageWithLayout(img, layout);
        blankColor_ = meta.blankColor;  // restore blank-fill so the recolour control tracks it
        canvas_->setBlankPage(!blankColor_.isEmpty());
        // Link the session; clear any local-project linkage so saves go to the server.
        // Unlinked (incognito deep-link) opens adopt the content only: no remote link,
        // no live co-edit, nothing ever pushed back — mirroring the browser's
        // copyServerProjectToIncognito semantics.
        activeProjectId_.clear();
        if (link) {
          remoteSession_->link().bind(serverUrl, id, meta.name, meta.color, meta.version);
        } else {
          remoteSession_->link().unbind();
          remoteSync_->stopRemotePoll();
        }
        currentSource_ = meta.source;
        currentResource_ = meta.resource;
        filterDirty_ = false;   // we just adopted the server/project filter
        refreshActions();
        // Fit the freshly-opened image to the window (matches the browser's switchToProject).
        // Skipped for a silent live-poll reload so a peer's edit doesn't reset zoom/pan.
        if (!silent) fitToWindow();
        if (link) remoteSync_->startRemotePoll();   // live co-edit: watch for peer changes
        // Chat persistence (§12): a linked, user-initiated open pulls the
        // project's server-stored chat (silent live-poll reloads must not stomp
        // the conversation mid-thought). Missing/invalid = a fresh scope.
        if (link && !silent && settings_.saveChatsWithProject) {
          if (auto* cc = connections_ ? connections_->find(serverUrl) : nullptr) {
            cc->downloadFileAsync(id, QStringLiteral("chat"),
                                  [this, self](bool cok, QByteArray data) {
                                    if (!self) return;
                                    restoreChatFromDoc(
                                        cok ? QJsonDocument::fromJson(data).object() : QJsonObject());
                                  });
          } else {
            restoreChatFromDoc(QJsonObject());
          }
        }
        if (!silent)
          notify_->success(QString("Opened \"%1\" from %2")
                               .arg(support::shortName(meta.name.isEmpty() ? QStringLiteral("Untitled") : meta.name),
                                    serverUrl));
      };
      c->downloadFileAsync(id, "original", [this, self, c, meta, adopt,
                                            reloadGuard](bool dok, QByteArray bytes) {
        if (!self) return;
        if (dok && !bytes.isEmpty()) {
          adopt(bytes);
          return;
        }
        // No stored bytes on the server (e.g. an extension-added project that only recorded the
        // image's web URL) — fetch that source URL directly. Qt Network has no CORS limit.
        fetchUrlBytesAsync(this, meta.source, [this, self, c, adopt, reloadGuard](QByteArray b) {
          if (!self) return;
          if (b.isEmpty()) {
            notify_->error(QString("Could not download image — %1").arg(c->lastError()));
            return;
          }
          adopt(b);
        });
      });
    });
  }

  // The current page format + x/y formulas (from global settings) as a layout-envelope meta.
  fileStore::LayoutMeta MainWindow::currentLayoutMeta() const {
    fileStore::LayoutMeta m;
    m.pageSize = settings_.pageSize;
    m.customPageWidth = settings_.customPageWidth;
    m.customPageHeight = settings_.customPageHeight;
    m.allowFormulas = settings_.allowFormulas;
    m.formulaX = settings_.formulaX;
    m.formulaY = settings_.formulaY;
    return m;
  }

  // Adopt a fetched layout's page format + formulas into the toolbar + settings (only the keys
  // it carries, so older projects keep the user's current page/formulas). Signals blocked.
  void MainWindow::adoptServerLayoutMeta(const QJsonObject& layout) {
    if (layout.contains("pageSize")) {
      const fileStore::LayoutMeta m = fileStore::parseLayoutMeta(layout);
      if (m.customPageWidth > 0) settings_.customPageWidth = m.customPageWidth;
      if (m.customPageHeight > 0) settings_.customPageHeight = m.customPageHeight;
      {
        QSignalBlocker bs(pageSize_);
        const int idx = pageSize_->findData(m.pageSize);
        if (idx >= 0) pageSize_->setCurrentIndex(idx);
      }
      settings_.pageSize = pageSizeValue();
      if (customGroup_) customGroup_->setVisible(settings_.pageSize == "custom");
      if (customW_ && customH_) {
        QSignalBlocker bw(customW_), bh(customH_);
        const double f = unitFormat().factor;
        customW_->setValue(settings_.customPageWidth * f);
        customH_->setValue(settings_.customPageHeight * f);
      }
    }
    if (layout.contains("allowFormulas") || layout.contains("formulaX") ||
        layout.contains("formulaY")) {
      const bool allow = layout.value("allowFormulas").toBool(false);
      // Keep the expressions regardless of the toggle (allow only gates visibility + applying).
      const QString fx = layout.value("formulaX").toString();
      const QString fy = layout.value("formulaY").toString();
      settings_.allowFormulas = allow;
      settings_.formulaX = fx;
      settings_.formulaY = fy;
      {
        QSignalBlocker ba(allowFormulas_);
        allowFormulas_->setChecked(allow);
      }
      if (actAllowFormulas_) {
        QSignalBlocker b(actAllowFormulas_);
        actAllowFormulas_->setChecked(allow);
      }
      if (formulaGroup_) formulaGroup_->setVisible(allow);
      {
        QSignalBlocker bx(formulaX_), by(formulaY_);
        formulaX_->setText(fx);
        formulaY_->setText(fy);
      }
      if (formulaError_) formulaError_->setVisible(false);
    }
    persistSettings();
  }

  void MainWindow::openProjectInNewWindow(const QString& id) {
    // A fresh window loads the saved projects from disk in its constructor, so it
    // already knows this project. It owns itself and is destroyed on close.
    auto* win = new MainWindow();
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    if (!win->loadProjectIntoCanvas(id)) {
      notify_->error("Could not open the project in a new window");
      win->close();  // auto-close the failed load
    }
  }

  bool MainWindow::projectOpenInOtherWindow(const QString& id) const {
    if (id.isEmpty()) return false;
    for (QWidget* w : QApplication::topLevelWidgets()) {
      auto* mw = qobject_cast<MainWindow*>(w);
      if (mw && mw != this && mw->activeProjectId_ == id) return true;
    }
    return false;
  }

  // Local↔server project transfer (move/copy to/from a server, + the import helper) lives in
  // ProjectTransferController (projectTransferController.hpp), constructed as projectTransfer_.

  // ── launch options (CLI) ──
  // Apply the parsed command-line options (gui/launchOptions.hpp). The desktop
  // counterpart of the browser's URL launch (applyExternalLaunch '#stencil=' +
  // applyProjectDeepLink '?open='). Runs after show(): the image/URL/video and
  // layout resolution is async, so it relies on the running event loop.
  void MainWindow::applyLaunchOptions(const LaunchOptions& opts) {
    if (opts.empty()) return;

    // Incognito is honored whenever we're NOT opening a saved project — a blank
    // incognito editor, or an incognito image. Set FIRST so it gates the theme
    // persist below and every write a subsequent load would trigger.
    if (opts.incognito && opts.project.isEmpty() && actIncognito_->isEnabled())
      actIncognito_->setChecked(true);  // drives incognito_ via its toggled slot

    // --theme dark|light: set + persist the default theme (persist is suppressed
    // while incognito, like every other settings write).
    if (opts.hasTheme) {
      settings_.themeMode = (opts.theme == "dark") ? "dark" : "light";
      applySettings(settings_, /*persist=*/true);
    }

    // Primary content priority: --project > a stencil:// server reference >
    // --src > a bare positional file.
    if (!opts.project.isEmpty()) {
      if (!openProjectByName(opts.project))
        notify_->error(QString("No project named \"%1\"").arg(opts.project));
    } else if (!opts.serverUrl.isEmpty() && !opts.serverProjectId.isEmpty()) {
      // Queued so the connect + download run on the event loop after show().
      const QString url = opts.serverUrl, id = opts.serverProjectId;
      const bool incog = opts.incognito;
      QTimer::singleShot(0, this, [this, url, id, incog] {
        openServerLaunch(url, id, incog);
      });
    } else if (!opts.src.isEmpty()) {
      pendingLaunchLayout_ = opts.layout;  // applied after the image loads
      pendingLaunchLayoutJson_ = opts.layoutJson;
      // A quick-crop override (Open-Image dialog "Open in new window" handoff): apply
      // the same page-aspect crop / whole-frame choice the user made in the preview,
      // instead of the default page-aspect auto-crop. Consumed by applyQuickCrop().
      if (opts.hasCropOverride)
        pendingCrop_ = opts.cropToPage
                           ? QuickCropOpts{QuickCropOpts::Mode::Page, opts.cropAlbum, opts.cropPage}
                           : QuickCropOpts{QuickCropOpts::Mode::None, false, QString()};
      openImageSource(opts.src, opts.frame);
    } else if (!opts.file.isEmpty()) {
      pendingLaunchLayout_ = opts.layout;
      openPathFromOS(opts.file, opts.frame);
    }

    // --projects: open the Projects window at launch. Queued so it runs after the
    // current call unwinds (and after a primary load has been kicked off).
    if (opts.projects) QTimer::singleShot(0, this, &MainWindow::openProjects);
  }

  // A stencil:// deep link arriving on a RUNNING app (macOS QFileOpenEvent url).
  // Same fields as a launch, minus the theme/projects extras.
  void MainWindow::openStencilUrl(const QUrl& url) {
    const LaunchOptions opts = parseStencilUrl(url);
    if (opts.empty()) {
      notify_->error("Could not read the stencil:// link");
      return;
    }
    applyLaunchOptions(opts);
  }

  // Deep-link server open: connect like a fresh manual client, then open the project.
  void MainWindow::openServerLaunch(const QString& serverUrl, const QString& id,
                                    bool incognito) {
    // normalizeBase throws no exceptions but yields "" on junk — guard it.
    const QString url = stencil::net::ServerClient::normalizeBase(serverUrl);
    if (url.isEmpty()) {
      notify_->error("Bad server URL in the link");
      return;
    }
    if (incognito && actIncognito_->isEnabled()) actIncognito_->setChecked(true);
    auto* mgr = ensureConnections();
    if (!mgr->find(url)) {
      // Reuse the saved token for this origin (the browser's saved-servers parity);
      // else connect tokenless and the server mints one (POST /auth/token).
      QString token;
      auto kind = stencil::net::ServerClient::CredentialKind::None;
      bool known = false;
      for (const auto& s : stencil::net::connectionStore::loadSavedServers()) {
        if (stencil::net::ServerClient::normalizeBase(s.url) == url) {
          token = s.token;
          kind = stencil::net::ServerClient::kindFromTag(s.kind);
          known = true;
          break;
        }
      }
      // A deep link can name ANY server — don't let a drive-by stencil:// URL
      // silently add a (persisted) connection to an origin this machine has never
      // used. Known origins (live or saved) skip the prompt.
      if (!known
          && QMessageBox::question(
                 this, "Open shared project",
                 QString("This link opens a shared project on %1.\nConnect to that server?")
                     .arg(url)) != QMessageBox::Yes) {
        return;
      }
      QString err;
      if (!mgr->connectTo(url, token, err, kind)) {
        // The normal connect path: surface the failure and open the Servers dialog
        // so the user can supply a token / fix the URL.
        notify_->error(QString("Could not connect to %1 — %2").arg(url, err));
        openConnections();
        return;
      }
      warnInsecureConnections();
    }
    openServerProject(url, id, /*silent=*/false, /*link=*/!incognito);
  }

  // "Open in…" — mirror the current session into the browser app or the Telegram
  // bot. A server-linked session sends only the server reference (the receiver
  // connects like a fresh client — no token in any link); a local/incognito session
  // embeds the image + full layout inline in the browser fragment. Telegram is
  // server-projects-only (image bytes can't ride a 64-char start payload).
  void MainWindow::openInAnotherApp() {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");
      return;
    }
    const bool serverProject = !remoteSession_->link().address.isEmpty() && !remoteSession_->link().id.isEmpty();
    const QString botUsername = settings_.telegramBotUsername.trimmed();
    const bool browserAvailable = !settings_.browserBaseUrl.trimmed().isEmpty();
    const bool telegramAvailable = !botUsername.isEmpty() && serverProject;
    if (!browserAvailable && !telegramAvailable) {
      // Shouldn't happen (actOpenIn_ is hidden when nothing's available), but guard.
      notify_->info("Nothing to open into — set a browser URL in Settings "
                    "(or a Telegram bot for server projects).");
      return;
    }
    OpenInDialog dlg(this, serverProject, remoteSession_->link().address, browserAvailable, telegramAvailable, incognito_);
    if (execMaybePopover(dlg) != QDialog::Accepted) return;
    const bool incog = dlg.incognito();

    if (dlg.outcome() == OpenInDialog::Outcome::Telegram) {
      if (!serverProject) return;  // the dialog disables this outcome anyway
      const QString payload = deepLink::encodeTelegramStartPayload(remoteSession_->link().address, remoteSession_->link().id);
      if (payload.isEmpty()) {
        // 64-char overflow (very long host): hand over the manual recipe instead
        // of a dead link, and open the bot chat.
        QMessageBox::information(
            this, "Link too long for Telegram",
            QString("The server address doesn't fit a Telegram start link.\n"
                    "Open the bot chat and paste:\n\n/connect %1\n/fetch %2")
                .arg(remoteSession_->link().address, remoteSession_->link().id));
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://t.me/") + botUsername));
        return;
      }
      QDesktopServices::openUrl(QUrl(deepLink::buildTelegramLink(botUsername, payload)));
      return;
    }

    // Browser app.
    QJsonObject payload;
    if (serverProject) {
      QJsonObject server;
      server["url"] = remoteSession_->link().address;
      server["id"] = remoteSession_->link().id;
      if (remoteSession_->link().version > 0) server["version"] = remoteSession_->link().version;
      payload["server"] = server;
    } else {
      QByteArray png;
      QBuffer buf(&png);
      buf.open(QIODevice::WriteOnly);
      canvas_->originalImage().save(&buf, "PNG");
      payload["dataUrl"] =
          QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
      payload["name"] = projectBaseName() + ".png";
      payload["layout"] = fileStore::buildLayoutJson(
          canvas_->imageWidth(), canvas_->imageHeight(), canvas_->allLines(),
          settings_.imageFilter, settings_.filterColor, canvas_->cropRect(),
          canvas_->rotationQuarters(), currentLayoutMeta());
      if (!currentSource_.isEmpty()) payload["source"] = currentSource_;
      if (!currentResource_.isEmpty()) payload["resource"] = currentResource_;
    }
    if (incog) payload["incognito"] = true;

    const QString url =
        deepLink::buildBrowserLaunchUrl(settings_.browserBaseUrl, payload);
    // Inline hand-offs ride the OS launcher's argv, which tolerates far less than an
    // in-page URL: refuse absurd payloads, warn on large ones (server links stay tiny).
    if (!serverProject && url.size() > 1000000) {
      notify_->error(
          "Image too large to hand off inline — save it to a server and share the server link");
      return;
    }
    if (!serverProject && url.size() > 200000)
      notify_->info("Large image — the hand-off may fail; prefer saving to a server");
    QDesktopServices::openUrl(QUrl(url));
  }

  // Lazily construct + wire the async --src resolver (image / URL / video frame).
  void MainWindow::ensureMediaLoader() {
    if (mediaLoader_) return;
    mediaLoader_ = new MediaLoader(this);
    connect(mediaLoader_, &MediaLoader::loaded, this,
            &MainWindow::onLaunchImageLoaded);
    connect(mediaLoader_, &MediaLoader::failed, this, [this](const QString& msg) {
      pendingLaunchLayout_.clear();
      pendingLaunchLayoutJson_.clear();
      pendingProvSource_.clear();
      pendingProvResource_.clear();
      notify_->error(msg);
    });
  }

  void MainWindow::openImageSource(const QString& src, int frame) {
    // Inline data: URL (a browser→desktop stencil:// hand-off): decode directly —
    // MediaLoader resolves paths/URLs/video, not data URIs.
    if (src.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
      const int comma = src.indexOf(QLatin1Char(','));
      QImage img;
      bool ok = comma > 0;
      if (ok) {
        const QString meta = src.left(comma);
        const QByteArray payload = src.mid(comma + 1).toUtf8();
        const QByteArray bytes = meta.contains(QLatin1String(";base64"), Qt::CaseInsensitive)
                                     ? QByteArray::fromBase64(payload)
                                     : QByteArray::fromPercentEncoding(payload);
        ok = img.loadFromData(bytes);
      }
      if (!ok) {
        pendingLaunchLayout_.clear();
        pendingLaunchLayoutJson_.clear();
        notify_->error("Could not decode the inline image");
        return;
      }
      onLaunchImageLoaded(img, QString());
      return;
    }
    ensureMediaLoader();
    mediaLoader_->load(src, frame);
  }

  // Open a file handed in by the OS shell (file-association / "Open With" / drop):
  // a *.json is a layout (applied onto the current image), anything else is an
  // image or video opened via the --src path.
  void MainWindow::openPathFromOS(const QString& path, int frame) {
    if (path.isEmpty()) return;
    const QString suffix = QFileInfo(path).suffix();
    if (suffix.compare("json", Qt::CaseInsensitive) == 0) {
      applyLayoutFromSource(path);
      return;
    }
    // A whole .stencil project (double-click / drag / file arg) loads image + layout + theme.
    if (suffix.compare("stencil", Qt::CaseInsensitive) == 0) {
      openProjectFile(path);
      return;
    }
    openImageSource(path, frame);
  }

  // Open a portable .stencil project: decode its embedded ORIGINAL image, adopt its layout, provenance, and (only if present) theme. Mirrors browser DrawingApp.applyProjectFile.
  void MainWindow::openProjectFile(const QString& path) {
    QByteArray bytes;
    if (!readFileBytes(path, bytes)) {
      notify_->error("Could not read the project file");
      return;
    }
    fileStore::ProjectFileData pf;
    QString err;
    if (!fileStore::parseProjectFile(bytes, pf, &err)) {
      notify_->error("Invalid .stencil file: " + err);
      return;
    }
    QImage img;
    if (!img.loadFromData(pf.imageBytes)) {
      notify_->error("Could not decode the project image");
      return;
    }
    activeProjectId_.clear();   // an opened project file is a fresh editor (Save to Project keeps it)
    loadImageWithLayout(img, pf.layout, pf.imageBytes, pf.imageExt);
    currentSource_ = pf.source;
    currentResource_ = pf.resource;
    // Apply the file's theme only when it carried one, so opening a themeless project never
    // changes the user's current theme. A custom-hex accent is ignored (desktop uses presets).
    if (pf.hasTheme) {
      bool changed = false;
      if (pf.themeMode == "light" || pf.themeMode == "dark") {
        settings_.themeMode = pf.themeMode;
        changed = true;
      }
      if (!pf.themeAccent.isEmpty()) {
        for (const auto& preset : accentPresets()) {
          if (pf.themeAccent == preset.key) {
            settings_.accentColor = pf.themeAccent;
            changed = true;
            break;
          }
        }
      }
      if (changed) {
        applyTheme();
        fileStore::saveSettings(settings_);
      }
    }
    // Persist as a local file-origin project so it shows the bronze .stencil outline + badge in the Projects list.
    createLocalProject(pf.name, /*announce=*/false, /*fromFile=*/true);
    // Link this file as the project's live-sync target (auto-save + watch when live sync is on).
    linkStencilFile(path, bytes);
    // Chat persistence (§12.3): adopt the file's saved chat when the opt-in is
    // on (an absent one = a fresh scope), and carry it onto the new local record.
    if (settings_.saveChatsWithProject) {
      restoreChatFromDoc(pf.chat);
      if (!pf.chat.isEmpty()) {
        if (Project* pr = findProject(activeProjectId_.toStdString())) {
          pr->chat = buildActiveChatDoc();
          fileStore::saveProjects(projectList_);
        }
      }
    }
    fitToWindow();
  }

  // Serialize the current project to .stencil bytes (ORIGINAL image + layout + metadata + theme); shared by Save Project As and live-sync auto-save. Mirrors browser ExportService.saveProjectFile.
  void MainWindow::setSourceBytes(const QByteArray& bytes, const QString& ext) {
    sourceBytes_ = bytes;
    sourceExt_ = ext.trimmed().toLower();
  }

  // Read + retain a local image file's raw bytes so a later .stencil bundle embeds the untouched
  // original (lossless). Clears the retained source on a read failure or a missing suffix.
  void MainWindow::retainSourceFromFile(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    QByteArray bytes;
    if (!ext.isEmpty()) readFileBytes(path, bytes);
    setSourceBytes(bytes, ext);   // empty bytes ⇒ buildStencilBytes re-encodes from pixels
  }

  bool MainWindow::openProjectByName(const QString& name) {
    const QString want = name.trimmed();
    for (const auto& p : projectList_) {
      if (QString::fromStdString(p.meta.name).compare(want, Qt::CaseInsensitive) ==
          0)
        return loadProjectIntoCanvas(QString::fromStdString(p.meta.id));
    }
    return false;
  }

  // Adopt a resolved --src image. A local file keeps its path (so session/project
  // saves reference it); a remote image / video frame has no path, so it is
  // adopted in-memory (like a clipboard paste). The page aspect is applied first,
  // exactly as openImage() does, so the auto-crop matches the current page size.
  void MainWindow::onLaunchImageLoaded(const QImage& image,
                                       const QString& localPath) {
    // Provenance for this load (from loadImageByUrl); consumed once, then cleared.
    const QString provSource = pendingProvSource_;
    const QString provResource = pendingProvResource_;
    pendingProvSource_.clear();
    pendingProvResource_.clear();

    // Inline full-layout hand-off (browser→desktop "Open in…" of a local/incognito
    // project): the layout describes crop + rotation + filter + lines + page in the
    // ORIGINAL image's space, so adopt it exactly like a server project — NOT the
    // default auto-crop + lines-only import, which would prompt on a dimension mismatch,
    // drop the lines, and ignore the filter. The image always arrives in-memory (a
    // data: URL decoded in openImageSource), so `image` is set here.
    if (!pendingLaunchLayoutJson_.isEmpty()) {
      const QString json = pendingLaunchLayoutJson_;
      pendingLaunchLayoutJson_.clear();
      pendingLaunchLayout_.clear();   // an inline layout supersedes any --layout source
      QJsonParseError err{};
      const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
      if (err.error == QJsonParseError::NoError && doc.isObject() && !image.isNull()) {
        loadImageWithLayout(image, doc.object());
        currentSource_ = provSource;
        currentResource_ = provResource;
        refreshActions();
        fitToWindow();
        return;
      }
      notify_->error("Invalid layout in the stencil:// link"
                     + (err.error != QJsonParseError::NoError ? QStringLiteral(": ") + err.errorString()
                                                              : QString()));
      // Fall through to a plain image load below.
    }

    const core::PageSize page = naturalPageCm(pageSizeValue(),
                                              settings_.customPageWidth,
                                              settings_.customPageHeight);
    canvas_->setPageCm(page.width, page.height);
    bool ok = false;
    if (!localPath.isEmpty())
      ok = canvas_->loadImage(localPath);  // path-backed (keeps it for saves)
    if (ok) {
      retainSourceFromFile(localPath);   // lossless .stencil bundle from the untouched file
    } else {
      if (image.isNull()) {
        notify_->error("Failed to open the image");
        pendingLaunchLayout_.clear();
        return;
      }
      canvas_->loadFromImage(image);  // remote image / video frame (in-memory)
      setSourceBytes({}, {});         // in-memory frame/remote → re-encode on bundle
    }
    // Quick pre-load crop (links modal): override the default page-aspect auto-crop
    // with the chosen page + orientation, or load the full frame uncropped. Applies
    // equally to still images and extracted video frames. Consumed once.
    applyQuickCrop();
    // A new image's provenance replaces the previous one (a plain --src/OS open
    // carries none, so both clear). Saved to the project on the next create/save.
    currentSource_ = provSource;
    currentResource_ = provResource;
    activeProjectId_.clear();  // a fresh URL/video/OS-open load is a new editor
    refreshActions();
    fitToWindow();
    playImageArrival();

    // --layout: apply now that an image exists (applyLayoutJson needs one). This is the
    // path/URL --layout variant; the inline stencil:// layout is handled up top via the
    // full-adoption branch.
    if (!pendingLaunchLayout_.isEmpty()) {
      const QString src = pendingLaunchLayout_;
      pendingLaunchLayout_.clear();
      applyLayoutFromSource(src);
    }
    // Persist as a local project so it shows in Projects (after any --layout lines are
    // in). A remote image / video frame has no on-disk path — createLocalProject writes
    // the pixels to the state dir. Browser parity: the active editor is always saved.
    adoptCanvasAsLocalProject();
  }

  // Override the just-loaded image's default page-aspect crop with the quick-crop
  // choice from the load-by-URL dialog (extends the existing auto-crop with an
  // orientation override + page choice + a no-crop path). Mirrors the browser's
  // defaultCropRect(albumOverride) / noCrop load opts. No-op for Auto / no image.
  void MainWindow::applyQuickCrop() {
    const QuickCropOpts opts = pendingCrop_;
    pendingCrop_ = {};  // consume regardless of outcome
    if (!canvas_->hasImage() || opts.mode == QuickCropOpts::Mode::Auto) return;
    // A freshly loaded image is un-rotated, so the original IS the crop's pixel space.
    const QImage& orig = canvas_->originalImage();
    const double iw = orig.width();
    const double ih = orig.height();
    if (iw <= 0 || ih <= 0) return;
    if (opts.mode == QuickCropOpts::Mode::None) {
      canvas_->applyCrop({0.0, 0.0, iw, ih}, /*recalc=*/false);  // full frame, uncropped
      return;
    }
    // Page mode: reflect the chosen page in the toolbar control (keeps coords/crop
    // dialog consistent), then crop to that page in the chosen orientation.
    if (!opts.page.isEmpty()) {
      const int idx = pageSize_->findData(opts.page);
      if (idx >= 0) pageSize_->setCurrentIndex(idx);  // → onPageSizeChanged
    }
    const core::PageSize pg = naturalPageCm(pageSizeValue(),
                                            settings_.customPageWidth,
                                            settings_.customPageHeight);
    canvas_->setPageCm(pg.width, pg.height);
    const double aspect = core::cropAspect(pg.width, pg.height, opts.album);
    canvas_->applyCrop(core::centeredCrop(iw, ih, aspect), /*recalc=*/false);
  }

  // Load a layout JSON from a local path or an http(s) URL, then adopt it through
  // the shared applyLayoutJson() guards. Mirrors uploadLayout(), but the source is
  // given (no file dialog) and may be remote.
  void MainWindow::applyLayoutFromSource(const QString& src) {
    auto adopt = [this, src](const QByteArray& bytes) {
      QJsonParseError err{};
      const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
      if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        notify_->error("Invalid layout JSON: " + err.errorString());
        return;
      }
      dataExport_->applyLayoutJson(doc.object());
    };

    const QUrl url = QUrl::fromUserInput(src);
    if (url.scheme() == "http" || url.scheme() == "https") {
      net::fetch(this, url, adopt, [this](const QString& e) {
        notify_->error("Could not fetch --layout: " + e);
      });
      return;
    }
    // Local file (resolve the existing path, not fromUserInput's guess).
    const QString path =
        QFileInfo(src).exists() ? src : url.toLocalFile();
    QFile f(path.isEmpty() ? src : path);
    if (!f.open(QIODevice::ReadOnly)) {
      notify_->error("Could not read --layout file");
      return;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    adopt(bytes);
  }

  // ── OS-shell window spawners (Dock menu / drop targets) ──
  // Each opens a fresh, self-owned top-level window so the action is independent
  // of whichever window (or app-lifetime menu) triggered it.
  void MainWindow::openIncognitoWindow() {
    // restoreLast=false → a brand-new EMPTY editor, not the last session.
    auto* win = new MainWindow(nullptr, /*restoreLast=*/false);
    win->setAttribute(Qt::WA_DeleteOnClose);
    if (win->actIncognito_->isEnabled())
      win->actIncognito_->setChecked(true);  // no image yet → toggle allowed
    win->show();
  }

  void MainWindow::openProjectsWindow() {
    auto* win = new MainWindow();
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    QTimer::singleShot(0, win, &MainWindow::openProjects);
  }

  void MainWindow::openProjectWindowById(const QString& id) {
    auto* win = new MainWindow();
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
    if (!win->loadProjectIntoCanvas(id)) win->close();
  }

  // Rebuild the macOS Dock menu: New Incognito Editor · Open Projects · the most
  // recently updated projects. Connected to qApp so the lambdas outlive the window
  // that built the menu. A no-op off macOS (QMenu::setAsDockMenu is macOS-only).
  void MainWindow::refreshDockMenu() {
#ifdef Q_OS_MACOS
    if (!sDockMenu_) {
      sDockMenu_ = new QMenu();  // app-lifetime; owned by neither window
      sDockMenu_->setAsDockMenu();
    }
    sDockMenu_->clear();
    connect(sDockMenu_->addAction("New Incognito Editor"), &QAction::triggered,
            qApp, [] { MainWindow::openIncognitoWindow(); });
    connect(sDockMenu_->addAction("Open Projects…"), &QAction::triggered, qApp,
            [] { MainWindow::openProjectsWindow(); });

    // Recent projects: the most recently updated, newest first (proxy for
    // "recently opened"). Each opens in its own window, leaving others untouched.
    std::vector<Project> recents = projectList_;
    std::sort(recents.begin(), recents.end(), [](const Project& a, const Project& b) {
      return a.meta.updatedAt > b.meta.updatedAt;
    });
    constexpr std::size_t kMaxRecents = 8;
    if (recents.size() > kMaxRecents) recents.resize(kMaxRecents);
    if (!recents.empty()) {
      sDockMenu_->addSeparator();
      for (const auto& pr : recents) {
        const QString id = QString::fromStdString(pr.meta.id);
        const QString name = QString::fromStdString(pr.meta.name);
        connect(sDockMenu_->addAction(name), &QAction::triggered, qApp,
                [id] { MainWindow::openProjectWindowById(id); });
      }
    }
#endif
  }

  // ── drag-and-drop (Photoshop-style drop-to-open) ──
  namespace {
    // A droppable source resolved from a drag: a LOCAL file (keeps its path), a remote http(s)
    // URL (an image dragged from a browser page), or raw IMAGE bytes. Desktop can fetch remote
    // URLs freely (no browser CORS), so a cross-page image drag works here where the browser is
    // CORS-limited.
    struct DropSrc {
      enum Kind { None, LocalFile, Url, ImageData } kind = None;
      QString value;  // path (LocalFile) or url (Url); ImageData carries no string
    };
    DropSrc droppableSource(const QMimeData* m) {
      if (!m) return {};
      for (const QUrl& u : m->urls())
        if (u.isLocalFile()) return { DropSrc::LocalFile, u.toLocalFile() };
      for (const QUrl& u : m->urls()) {
        const QString s = u.toString();
        if (s.startsWith("http://") || s.startsWith("https://")) return { DropSrc::Url, s };
      }
      if (m->hasText()) {
        const QString t = m->text().trimmed();
        if (t.startsWith("http://") || t.startsWith("https://")) return { DropSrc::Url, t };
      }
      if (m->hasImage()) return { DropSrc::ImageData, QString() };
      return {};
    }
  }  // namespace

  void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    // Accept a dragged local file (image / video / layout JSON), a remote image URL, or raw
    // image bytes. Show the split LEFT-save / RIGHT-incognito overlay.
    if (droppableSource(event->mimeData()).kind == DropSrc::None) return;
    event->acceptProposedAction();
    if (dropZones_) {
      dropZones_->setActiveLeft(event->position().x() < width() / 2.0);
      dropZones_->showZones();
    }
  }

  void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (droppableSource(event->mimeData()).kind == DropSrc::None) return;
    event->acceptProposedAction();
    if (dropZones_) dropZones_->setActiveLeft(event->position().x() < width() / 2.0);
  }

  void MainWindow::dragLeaveEvent(QDragLeaveEvent*) {
    if (dropZones_) dropZones_->hideZones();
  }

  void MainWindow::dropEvent(QDropEvent* event) {
    if (dropZones_) dropZones_->hideZones();
    const DropSrc src = droppableSource(event->mimeData());
    if (src.kind == DropSrc::None) return;
    event->acceptProposedAction();

    // A local .json layout ignores the save/incognito split (it applies drawing data).
    if (src.kind == DropSrc::LocalFile &&
        QFileInfo(src.value).suffix().compare("json", Qt::CaseInsensitive) == 0) {
      applyLayoutFromSource(src.value);
      return;
    }

    // RIGHT half = incognito, LEFT half = upload + save.
    const bool incognito = event->position().x() >= width() / 2.0;

    // Resolve a source string: local path, remote URL, or a data: URL for raw dropped pixels
    // (openImageSource decodes data: URLs), plus whether new-window is offerable.
    QString source = src.value;
    bool isLocal = false;
    if (src.kind == DropSrc::LocalFile) { isLocal = true; }
    else if (src.kind == DropSrc::ImageData) {
      const QImage img = qvariant_cast<QImage>(event->mimeData()->imageData());
      if (img.isNull()) return;
      source = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(pngBytes(img).toBase64());
    }

    // Open the dropped image via the LEFT (save) or RIGHT (incognito) path. Local files keep
    // their path (openImageHere/InNewWindow); URLs + raw pixels go through the async source path.
    const auto openHere = [&] { if (isLocal) openImageHere(source, incognito); else openSourceHere(source, -1, incognito); };
    const auto openNew = [&] { if (isLocal) openImageInNewWindow(source, incognito); else openSourceInNewWindow(source, -1, incognito); };

    // An image already open → ask this window vs a new one (mirrors the browser modal).
    if (canvas_->hasImage()) {
      QMessageBox box(this);
      box.setWindowTitle(tr("Open dropped image"));
      box.setText(tr("An image is already open. Where should the dropped image open?"));
      QPushButton* hereBtn = box.addButton(tr("This window"), QMessageBox::AcceptRole);
      QPushButton* newBtn = box.addButton(tr("New window"), QMessageBox::ActionRole);
      QAbstractButton* dropCancel = box.addButton(QMessageBox::Cancel);
      const QColor dropTxt = box.palette().color(QPalette::WindowText);
      hereBtn->setIcon(themedIcon("image", dropTxt, 15));      // browser: confirmIcon 'image'
      newBtn->setIcon(themedIcon("external", dropTxt, 15));    // …opened in another window
      if (dropCancel) dropCancel->setIcon(themedIcon("x", dropTxt, 15));
      box.exec();
      if (box.clickedButton() == hereBtn) openHere();
      else if (box.clickedButton() == newBtn) openNew();
      return;
    }
    openHere();
  }

  // A fresh image ASSEMBLES from dust (Sweep::Gather; browser ghostIn). The
  // canvas waits at opacity 0, effect torn down at the end (an opacity effect
  // must never stay on a repainting canvas); snapshot BEFORE the effect goes on.
  void MainWindow::playImageArrival() {
    if (!canvas_) return;

    const bool dust = canvas_->hasImage() && scroll_ && scroll_->viewport()
        && !canvas_->visibleRegion().boundingRect().isEmpty()
        && DisintegrateOverlay::overRect(canvas_, canvas_->visibleRegion().boundingRect(),
                                         scroll_->viewport(), DisintegrateOverlay::Sweep::Gather);
    auto* fx = new QGraphicsOpacityEffect(canvas_);
    fx->setOpacity(0.0);
    canvas_->setGraphicsEffect(fx);
    if (dust) {
      // Hidden for the whole flight, then simply revealed — the motes have already drawn
      // it into place, so fading it up as well would double the arrival.
      QTimer::singleShot(DisintegrateOverlay::kMs, canvas_,
                         [this] { if (canvas_) canvas_->setGraphicsEffect(nullptr); });
      return;
    }
    // No dust to play (a canvas not on screen yet, or too small to tile): fall back to
    // the plain fade rather than to a hidden canvas.
    auto* anim = new QVariantAnimation(canvas_);
    anim->setDuration(360);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QVariantAnimation::valueChanged, canvas_,
            [fx](const QVariant& v) { fx->setOpacity(v.toDouble()); });
    connect(anim, &QVariantAnimation::finished, canvas_,
            [this] { if (canvas_) canvas_->setGraphicsEffect(nullptr); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

  // View/edit/open/remove the current image's source & resource links, or add a
  // new image by URL. Edits to the links persist to the active project (and the
  // in-memory current* provenance); a URL load routes through loadImageByUrl().
  void MainWindow::openLinks() {
    // Image Links only edits a loaded image's provenance; the action is disabled
    // without one (refreshActions), so this is only reached with an image.
    // Seed from the active project's stored provenance when available, else from
    // the live current* provenance (e.g. an image just loaded by URL, not yet saved).
    QString src = currentSource_, res = currentResource_;
    if (!activeProjectId_.isEmpty()) {
      Project* pr = findProject(activeProjectId_.toStdString());
      if (pr) {
        src = QString::fromStdString(pr->meta.source);
        res = QString::fromStdString(pr->meta.resource);
      }
    }

    LinksDialog dlg(src, res, canvas_->hasImage(), settings_.pageSize,
                    settings_.units, this);
    if (execMaybePopover(dlg) != QDialog::Accepted) return;

    if (dlg.loadRequested()) {
      // Quick pre-load edits: crop to the chosen page aspect/orientation, or load
      // the full frame uncropped — consumed once by onLaunchImageLoaded.
      if (dlg.cropToPage())
        pendingCrop_ = {QuickCropOpts::Mode::Page, dlg.cropAlbum(), dlg.cropPageSize()};
      else
        pendingCrop_ = {QuickCropOpts::Mode::None, false, QString()};
      // The dialog already fetched and decoded the exact image/frame for its
      // preview — adopt those pixels directly (no second download/seek) so what
      // was previewed is exactly what loads. Fall back to a fresh resolve only if
      // the preview image is somehow absent.
      const QImage previewed = dlg.previewedImage();
      if (!previewed.isNull()) {
        pendingProvSource_ = dlg.urlSource();
        pendingProvResource_ = dlg.urlResource();
        onLaunchImageLoaded(previewed, QString());
      } else {
        loadImageByUrl(dlg.urlSource(), dlg.urlResource(), dlg.urlFrame());
      }
      return;
    }

    // No image → the dialog was in add-by-URL mode and just closed; nothing to save.
    if (!canvas_->hasImage()) return;

    // Plain OK: persist the edited links onto the current image + active project.
    currentSource_ = dlg.source();
    currentResource_ = dlg.resource();
    if (!activeProjectId_.isEmpty()) {
      Project* pr = findProject(activeProjectId_.toStdString());
      if (pr) {
        pr->meta.source = currentSource_.toStdString();
        pr->meta.resource = currentResource_.toStdString();
        pr->meta.updatedAt = nowMs();
        fileStore::saveProjects(projectList_);
        notify_->success("Links saved");
        return;
      }
    }
    notify_->info("Links updated — save to a project to keep them");
  }

  // Load an image/video by URL (reusing the --src resolver), remembering the URL +
  // optional resource as provenance so the next project save records them.
  void MainWindow::loadImageByUrl(const QString& source, const QString& resource,
                                  int frame) {
    if (source.isEmpty()) return;
    pendingProvSource_ = source;
    pendingProvResource_ = resource;
    openImageSource(source, frame);
  }

  void MainWindow::newProjectFromCanvas() {
    if (incognito_) {  // an explicit promotion: leave incognito and keep the work
      const QString promoted = promoteIncognitoToLocal();
      if (!promoted.isEmpty()) {
        notify_->success(QStringLiteral("Left incognito — saved \"%1\"")
                             .arg(support::shortName(promoted)));
        return;
      }
      notify_->info("Nothing to save yet");
      return;
    }
    // Seed the name from the image filename (mirrors the browser, where a new project
    // is named after the image), else a unique "Untitled N".
    QString seed = canvas_->hasImage() ? canvas_->imageBaseName() : QString();
    if (seed.isEmpty()) {
      std::vector<core::ProjectMeta> metas;
      for (const auto& pr : projectList_) metas.push_back(pr.meta);
      core::ProjectsStore tmp;
      tmp.load(metas);
      seed = QString::fromStdString(tmp.defaultName());
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Project",
                                               "Project name:", QLineEdit::Normal,
                                               seed, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    const auto check = checkProjectName(name.trimmed(), QString());
    if (!check.ok) {
      notify_->error(QString::fromStdString(check.reason));
      return;
    }
    createProject(name.trimmed());
  }

  // Find a loaded project by id, or nullptr when none matches.
  Project* MainWindow::findProject(const std::string& id) {
    auto it = std::find_if(projectList_.begin(), projectList_.end(),
                           [&](const Project& p) { return p.meta.id == id; });
    return it == projectList_.end() ? nullptr : &*it;
  }

  // Remove ONE local project row, resetting the editor when it is the open one
  // (browser removeProject → storage.newTemporary: clearing only the id left its
  // image, lines and name sitting there as a project that no longer exists). The
  // caller persists + refreshes after its batch.
  void MainWindow::eraseLocalProject(const QString& id) {
    const std::string sid = id.toStdString();
    projectList_.erase(
        std::remove_if(projectList_.begin(), projectList_.end(),
                       [&](const Project& p) { return p.meta.id == sid; }),
        projectList_.end());
    if (activeProjectId_ == id) resetToBlankEditor();
  }

  // Persist settings to disk unless this is an incognito window (which never writes).
  void MainWindow::persistSettings() {
    if (!incognito_) fileStore::saveSettings(settings_);
  }

  // Create-project entry point. With ≥1 server connected, ask where to save it:
  // this computer (local) or one of the connected servers. Otherwise save locally.
  // The incognito guard lives at each call site. Mirrors the browser's local-vs-
  // server target choice in the open/blank flows.
  void MainWindow::createProject(const QString& name) {
    const QStringList servers = connections_ ? connections_->urls() : QStringList();
    if (servers.isEmpty()) {
      createLocalProject(name);
      return;
    }
    QStringList targets;
    targets << tr("This computer (local)");
    for (const QString& s : servers) targets << tr("Server: %1").arg(s);
    bool ok = false;
    const QString choice =
        QInputDialog::getItem(this, tr("Save project"), tr("Where should it be saved?"),
                              targets, 0, false, &ok);
    if (!ok) return;
    const int idx = targets.indexOf(choice);
    if (idx <= 0) {
      createLocalProject(name);
    } else {
      createServerProject(servers.at(idx - 1), name);
    }
  }

  // Build a Project from the current canvas, persist it, mark it active, refresh,
  // and notify. pr.meta.name == the passed name.
  void MainWindow::createLocalProject(const QString& name, bool announce, bool fromFile) {
    remoteSession_->link().unbind();  // a freshly created local project is not server-linked
    remoteSync_->stopRemotePoll();   // no longer a server session
    Project pr;
    pr.meta.id = projectsStore_.createId(nowMs(), makeSalt());
    pr.meta.name = name.toStdString();
    pr.meta.createdAt = pr.meta.updatedAt = nowMs();
    pr.meta.expiresAt = core::ProjectsStore::addPeriod(
        pr.meta.updatedAt, core::ProjectsStore::DEFAULT_PERIOD);
    // A blank / remote / video-frame canvas has no on-disk path; write the original
    // pixels to the state dir so the project reloads them (crop + rotation are stored
    // separately as meta, so persist the UNCROPPED original). Also point the canvas at
    // the new path so later session/project saves round-trip it.
    QString path = canvas_->imagePath();
    if (path.isEmpty() && canvas_->hasImage()) {
      const QString imgDir = fileStore::stateDir() + "/images";
      QDir().mkpath(imgDir);
      path = imgDir + "/" + QString::fromStdString(pr.meta.id) + ".png";
      if (canvas_->originalImage().save(path, "PNG")) canvas_->setImagePath(path);
      else path.clear();  // write failed → keep it in-memory (hasImage=false)
    }
    pr.imagePath = path;
    pr.lines = canvas_->allLines();
    pr.cropRect = canvas_->cropRect();
    pr.rotationQuarters = canvas_->rotationQuarters();
    pr.meta.hasImage = !pr.imagePath.isEmpty();
    pr.meta.source = currentSource_.toStdString();
    pr.meta.resource = currentResource_.toStdString();
    pr.meta.blankColor = blankColor_.toStdString();  // blank-fill colour (empty = ordinary image)
    pr.meta.blank = !blankColor_.isEmpty();
    pr.meta.fromFile = fromFile;  // provenance: opened from a .stencil (bronze projects-list outline)
    stampCanvasMeta(pr.meta);     // cache image px dims + line length (cm) for the projects-list tooltip
    projectList_.push_back(pr);
    activeProjectId_ = QString::fromStdString(pr.meta.id);
    fileStore::saveProjects(projectList_);
    refreshActions();
    refreshDockMenu();  // surface the new project in the Dock "recent" list
    if (announce) notify_->success(QString("Created \"%1\"").arg(support::shortName(name)));
  }

  void MainWindow::adoptCanvasAsLocalProject() {
    // Guards: incognito never persists; a server session owns its own saving; an
    // already-active project means this canvas is that project (open/replace), not a
    // fresh load; and there's nothing to save without an image.
    if (incognito_) return;
    if (!activeProjectId_.isEmpty() || !remoteSession_->link().address.isEmpty()) return;
    if (!canvas_->hasImage()) return;
    // Name after the image file, else a unique "Untitled N" (mirrors newProjectFromCanvas).
    QString seed = canvas_->imageBaseName();
    if (seed.isEmpty()) {
      std::vector<core::ProjectMeta> metas;
      for (const auto& pr : projectList_) metas.push_back(pr.meta);
      core::ProjectsStore tmp;
      tmp.load(metas);
      seed = QString::fromStdString(tmp.defaultName());
    }
    createLocalProject(seed, /*announce=*/false);  // the load path already notified
    // Browser parity: storage.save() flashes "Saved" whenever a project persists.
    notify_->success(QStringLiteral("Saved"));
  }

  // Create the project on `serverUrl` (POST /projects), upload the current image as
  // the 'original', and link the session so a later Save writes back. Mirrors the
  // browser's createRemoteProject (remoteSync.js).
  void MainWindow::createServerProject(const QString& serverUrl, const QString& name,
                                       std::function<void()> onLinked) {
    stencil::net::ServerClient* c = remoteSession_->requireClient(serverUrl);
    if (!c) return;
    const bool hasImage = canvas_->hasImage();
    const int w = hasImage ? canvas_->imageWidth() : 0;
    const int h = hasImage ? canvas_->imageHeight() : 0;
    QPointer<MainWindow> self(this);
    c->createProjectAsync(
        name, currentSource_, currentResource_, hasImage, w, h,
        [this, self, c, serverUrl, name, hasImage, w, h, onLinked](bool ok, QString id,
                                                                   qint64 version) {
          if (!self) return;
          if (!ok) {
            notify_->error(QString("Could not create on server — %1").arg(c->lastError()));
            return;
          }
          // Link the session (this is now a server project, not a local one) + finish. A freshly
          // created server project has no custom colour yet. `onLinked` fires only on success.
          auto link = [this, self, serverUrl, id, name, onLinked](qint64 v) {
            if (!self) return;
            activeProjectId_.clear();
            remoteSession_->link().bind(serverUrl, id, name, QString(), v);
            refreshActions();
            notify_->success(QString("Created \"%1\" on %2").arg(name, serverUrl));
            if (onLinked) onLinked();
          };
          if (!hasImage) {
            link(version);
            return;
          }
          const QByteArray bytes = pngBytes(canvas_->image());
          c->uploadFileAsync(id, "original", bytes, "png", w, h,
                             [this, self, c, id, version, link](bool uok) {
                               if (!self) return;
                               if (!uok) {
                                 notify_->error(QString("Created, but image upload failed — %1")
                                                    .arg(c->lastError()));
                                 // Still link below so the user can retry via Save.
                                 link(version);
                                 return;
                               }
                               // The file write bumps the version; re-read it so the next save's
                               // guard is accurate (mirrors remoteSync.currentVersion()).
                               c->getProjectAsync(id, [self, version, link](
                                                          bool gok, stencil::net::ServerProject meta,
                                                          QJsonObject) {
                                 if (!self) return;
                                 link(gok ? meta.version : version);
                               });
                             });
        });
  }

  // Save a server-linked session back: version-guarded name/layout PUT, then upload
  // the rendered result. A 409 surfaces a clear "edited elsewhere" message and
  // leaves the link untouched. Mirrors the browser's saveToServer/saveRemoteProject.
  void MainWindow::saveToServer() {
    if (!settings_.syncToServer) return;  // sync off — fetched project stays edit-in-memory only
    stencil::net::ServerClient* c = remoteSession_->requireClient(
        remoteSession_->link().address, QString("Not connected to %1 — reconnect it first").arg(remoteSession_->link().address));
    if (!c) return;
    // Guard the poll for the whole push (async in flight) so we don't reload our own change. The
    // shared clearer sets remotePushing_ false once the last pending continuation is gone — every
    // exit path (commit, conflict, hard error, or the client/window destroyed mid-flight).
    remotePushing_ = true;
    auto pushGuard = std::shared_ptr<void>(nullptr, [self = QPointer<MainWindow>(this)](void*) {
      if (self) self->remotePushing_ = false;
    });
    QPointer<MainWindow> self(this);
    const int w = canvas_->imageWidth();
    const int h = canvas_->imageHeight();
    // Concurrent co-edit: on a version-guard conflict, merge the server's latest lines with
    // ours and retry — looping (up to 6 attempts) so a tight race (incl. the result upload's
    // extra version bump) still converges with both editors' annotations intact. The
    // read→PUT→retry loop is the shared primitive; the line-union merge below is this save's
    // conflict-resolution policy.
    using GO = stencil::net::ServerClient::GuardOutcome;
    stencil::net::ServerClient::runGuardedWriteAsync(
        /*attempts=*/6, /*startVersion=*/remoteSession_->link().version,
        [this, self, c, w, h, pushGuard](qint64 version, std::function<void(GO)> cb) {
          if (!self) { cb(GO::Failed); return; }
          const QJsonObject layout =
              fileStore::buildLayoutJson(w, h, canvas_->allLines(),
                                         settings_.imageFilter, settings_.filterColor,
                                         canvas_->cropRect(), canvas_->rotationQuarters(),
                                         currentLayoutMeta());
          c->updateProjectAsync(
              remoteSession_->link().id, remoteSession_->link().name, layout, version,
              [this, self, c, cb](bool ok, qint64 newVersion, bool conflict) {
                if (!self) { cb(GO::Failed); return; }
                if (ok) {
                  remoteSession_->link().version = newVersion;
                  cb(GO::Committed);
                  return;
                }
                if (!conflict) {
                  notify_->error(QString("Server save failed — %1").arg(c->lastError()));
                  cb(GO::Failed);
                  return;
                }
                cb(GO::Conflict);
              });
        },
        [this, self, c, pushGuard](qint64 /*version*/, std::function<void(bool, qint64)> cb) {
          if (!self) { cb(false, 0); return; }
          // Pull the peer's latest, union-merge their lines into ours (deduped), adopt the
          // server version, and retry.
          c->getProjectAsync(
              remoteSession_->link().id,
              [this, self, cb](bool ok, stencil::net::ServerProject meta, QJsonObject srvLayout) {
                if (!self || !ok) { cb(false, 0); return; }  // give up (re-read failed)
                int sw = 0, sh = 0;
                core::Lines mlines = fileStore::parseLayoutJson(srvLayout, sw, sh);
                QSet<QString> seen;
                for (const auto& l : mlines) seen.insert(lineKey(l));
                for (const auto& l : canvas_->allLines()) {
                  const QString k = lineKey(l);
                  if (!seen.contains(k)) { mlines.push_back(l); seen.insert(k); }
                }
                {  // apply merged lines (+ peer filter) locally without re-triggering a push.
                  // Synchronous block: the reload flag brackets it (onCanvasChanged reads it).
                  remoteReloading_ = true;
                  canvas_->setLines(mlines);
                  // Adopt the peer's filter UNLESS this user changed their own, so a line-only
                  // edit doesn't clobber the peer's filter change (the scalar can't merge).
                  if (!filterDirty_) {
                    QString sf, st;
                    parseLayoutFilter(srvLayout, settings_.filterColor, sf, st);
                    applyTintColor(QColor(st));
                    applyImageFilter(sf);
                  }
                  remoteReloading_ = false;
                }
                remoteSession_->link().version = meta.version;
                cb(true, meta.version);
              });
        },
        [this, self, c, w, h, pushGuard](GO outcome) {
          if (!self) return;
          // A hard (non-409) failure already notified inside the attempt and stops here; a
          // lingering Conflict means the attempts were exhausted (or a re-read failed).
          if (outcome == GO::Failed) return;
          if (outcome != GO::Committed) {
            notify_->error(
                "This project was edited elsewhere — reload it from the server before "
                "saving again");
            return;
          }
          filterDirty_ = false;   // our filter (if any) is now the server's
          // Confirm our own save (the union-merge kept both editors' annotations intact). Fired
          // after the result upload + version refresh, matching the previous synchronous order.
          auto announce = [this, self, pushGuard]() {
            if (self)
              notify_->success(QString("Saved \"%1\" to %2")
                                   .arg(remoteSession_->link().name, remoteSession_->link().address));
          };
          // Upload the annotated render as the 'result'. The file write bumps the version, so
          // re-read it to keep the guard accurate for the next save.
          if (canvas_->hasImage()) {
            const QByteArray bytes = pngBytes(canvas_->renderToImage(true));
            c->uploadFileAsync(
                remoteSession_->link().id, "result", bytes, "png", w, h,
                [this, self, c, announce, pushGuard](bool uok) {
                  if (!self) return;
                  if (!uok) { announce(); return; }
                  c->getProjectAsync(remoteSession_->link().id,
                                     [this, self, announce](bool gok, stencil::net::ServerProject meta,
                                                            QJsonObject) {
                                       if (!self) return;
                                       if (gok) remoteSession_->link().version = meta.version;
                                       announce();
                                     });
                });
          } else {
            announce();
          }
        });
  }

  void MainWindow::saveToActiveProject() {
    if (incognito_) {  // S6: no local save while incognito…
      const QStringList servers = connections_ ? connections_->urls() : QStringList();
      if (!canvas_->hasImage()) {
        notify_->info("Nothing to save yet");
        return;
      }
      if (servers.isEmpty()) {   // no server to publish to — keep it locally instead
        notify_->success(QStringLiteral("Left incognito — saved \"%1\"")
                             .arg(support::shortName(promoteIncognitoToLocal())));
        return;
      }
      // …but it CAN be published to a server (it then becomes a normal server-backed project
      // and leaves incognito), mirroring the browser's incognito "Save to server".
      QString target = servers.first();
      if (servers.size() > 1) {
        bool ok = false;
        target = QInputDialog::getItem(this, tr("Save to server"),
                                       tr("Publish this incognito project to which server?"),
                                       servers, 0, false, &ok);
        if (!ok) return;
      }
      publishIncognitoToServer(target);
      return;
    }
    if (!remoteSession_->link().address.isEmpty()) {  // server-linked session → write back to the server
      if (!settings_.syncToServer) {
        notify_->info(
            "Sync off — not saved. Export the image/layout or use Make local copy to keep changes.");
        return;
      }
      saveToServer();
      return;
    }
    if (activeProjectId_.isEmpty()) {
      newProjectFromCanvas();
      return;
    }
    Project* pr = findProject(activeProjectId_.toStdString());
    if (!pr) {
      newProjectFromCanvas();
      return;
    }
    pr->imagePath = canvas_->imagePath();
    pr->lines = canvas_->allLines();
    pr->cropRect = canvas_->cropRect();
    pr->rotationQuarters = canvas_->rotationQuarters();
    pr->meta.updatedAt = nowMs();
    pr->meta.hasImage = !pr->imagePath.isEmpty();
    stampCanvasMeta(pr->meta);  // refresh cached image px dims + line length (cm) for the tooltip
    // Keep provenance unless the active image carries its own (a save shouldn't
    // wipe links set via the Links dialog, but a fresh URL-loaded image updates them).
    if (!currentSource_.isEmpty()) pr->meta.source = currentSource_.toStdString();
    if (!currentResource_.isEmpty()) pr->meta.resource = currentResource_.toStdString();
    // Chat persistence (§12): the saved copy mirrors the current conversation
    // when the opt-in is on; with it off, an earlier saved chat is left alone.
    if (settings_.saveChatsWithProject) pr->chat = buildActiveChatDoc();
    fileStore::saveProjects(projectList_);
    refreshDockMenu();  // bump it to the top of the Dock "recent" list
    notify_->success(
        QString("Saved to \"%1\"").arg(support::shortName(QString::fromStdString(pr->meta.name))));
  }

  // Trash button — mirrors the browser #clear-storage handler (controlsBinder.js).
  // The button is hidden for server-linked sessions (refreshActions), so this only
  // ever runs for a local project or a temporary/blank editor.
  void MainWindow::clearCurrentProject() {
    const bool hasProject = !activeProjectId_.isEmpty();
    const QString title = hasProject ? tr("Clear project") : tr("Clear editor");
    const QString msg = hasProject
        ? tr("Clear this project (image + lines) from storage?")
        : tr("Clear this editor (image + lines)?");
    if (QMessageBox::question(this, title, msg,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
      return;
    if (hasProject) {
      // Remove the active LOCAL project from the store (same plumbing as the projects
      // dialog's per-project Remove), then reset to a blank editor.
      const std::string id = activeProjectId_.toStdString();
      projectList_.erase(
          std::remove_if(projectList_.begin(), projectList_.end(),
                         [&](const Project& p) { return p.meta.id == id; }),
          projectList_.end());
      fileStore::saveProjects(projectList_);
    }
    resetToBlankEditor();
    refreshDockMenu();  // drop the cleared project from the Dock "recent" list
    // A success, not a notice: the clear did what was asked (browser controlsBinder.js
    // shows the same two strings in --success).
    notify_->success(hasProject ? "Project cleared" : "Editor cleared");
  }

  // Reset the editor to the empty "Open an image" canvas — the desktop equivalent of
  // the browser's storage.newTemporary(): drop the image, lines, project binding and
  // provenance. (link().unbind() is defensive; the trash button is hidden for server
  // sessions, so a link is never set here.)
  void MainWindow::resetToBlankEditor() {
    activeProjectId_.clear();
    remoteSession_->link().unbind();
    currentSource_.clear();
    currentResource_.clear();
    blankColor_.clear();
    // The image scatters (browser ghostOut): snapshot BEFORE clearImage repaints.
    // Hosted on the scroll VIEWPORT and confined to visibleRegion() — a
    // window-parented overlay spilled across the panel and the chat dock.
    if (canvas_ && scroll_ && scroll_->viewport()) {
      const QRect vis = canvas_->visibleRegion().boundingRect();
      if (!vis.isEmpty())
        DisintegrateOverlay::overRect(canvas_, vis, scroll_->viewport(),
                                      DisintegrateOverlay::Sweep::Fall);
    }
    canvas_->clearImage();
    updateStatusIdle();   // the last hovered pixel must not outlive the image it named
    // …and keep the empty-canvas invitation off screen until the dust has landed, or the
    // "click to create a blank image" box appears underneath the falling particles and the
    // clear reads as happening twice (browser parity: .canvas-clearing).
    canvas_->setIdleHintHidden(true);
    QTimer::singleShot(DisintegrateOverlay::kMs, canvas_,
                       [this] { if (canvas_) canvas_->setIdleHintHidden(false); });
    refreshActions();
    saveSessionNow();  // persist the cleared state so it doesn't restore on next launch
  }

  // ── Project name surface (window title + toolbar field) ──

  QString MainWindow::activeProjectName() const {
    if (activeProjectId_.isEmpty()) return {};
    for (const auto& p : projectList_)
      if (QString::fromStdString(p.meta.id) == activeProjectId_)
        return QString::fromStdString(p.meta.name);
    return {};
  }

  QString MainWindow::projectBaseName() const {
    const QString n = activeProjectName();
    if (!n.isEmpty()) return n;   // the project name IS the download name
    return canvas_ ? canvas_->imageBaseName() : QStringLiteral("image");
  }

  core::ProjectsStore::NameCheck MainWindow::checkProjectName(
      const QString& name, const QString& exceptId) const {
    std::vector<core::ProjectMeta> metas;
    for (const auto& p : projectList_) metas.push_back(p.meta);
    core::ProjectsStore store;   // local; never disturbs projectsStore_
    store.load(metas);
    return store.validateName(name.toStdString(), exceptId.toStdString());
  }

  bool MainWindow::renameProjectById(const QString& id, const QString& rawName) {
    const QString name = rawName.trimmed();
    Project* pr = findProject(id.toStdString());
    if (!pr) return false;
    const auto check = checkProjectName(name, id);
    if (!check.ok) {
      notify_->error(QString::fromStdString(check.reason));
      return false;
    }
    pr->meta.name = name.toStdString();
    // The project name is THE name: downloads use projectBaseName(), so there is no
    // separate image name to keep in sync.
    fileStore::saveProjects(projectList_);
    refreshDockMenu();
    if (activeProjectId_ == id) updateProjectTitle();
    notify_->success(QString("Renamed to \"%1\"").arg(support::shortName(name)));
    return true;
  }

  // Header-row "Image Size: W × H px" (+ "· blank"), or a neutral hint when no image is loaded.
  // Always visible — the header row never collapses — mirroring the browser's #image-info bar.
  // The incognito half of the image-info line: a muted "|" divider, then the app's OWN
  // incognito glyph (the one the toolbar toggle wears — never an emoji, which rendered
  // in the font's colour and style) tinted like the accent tag beside it. Divider and
  // tag are one unit: nothing here is ever emitted without the rest, so a plain line
  // can't end in a dangling separator.
  QString MainWindow::incognitoTagHtml() const {
    const Palette pal = themePalette(resolveDark(settings_.themeMode), settings_.accentColor);
    const int glyphPx = std::max(12, QFontMetrics(imageSizeInfo_->font()).height() - 2);
    return QStringLiteral("&nbsp;&nbsp;<span style=\"color:%1;\">|</span>&nbsp;&nbsp;"
                          "%2<span style=\"color:%3;font-weight:700;vertical-align:middle;\">"
                          "&nbsp;Incognito &mdash; not saved</span>")
        .arg(pal.textMuted.name(),
             inlineIconHtml("incognito", pal.accent, glyphPx,
                            QStringLiteral("vertical-align:middle")),
             pal.accent.name());
  }

  // Hold the info row at its TALLER state's height: the incognito glyph is ~2px
  // taller than plain text, and it must not shift the rows below. Measured on a
  // throwaway twin (the live label would flicker); cached until font/theme changes.
  void MainWindow::reserveImageInfoHeight() {
    if (!imageSizeInfo_) return;
    const QString key = imageSizeInfo_->font().key() + QLatin1Char('|') +
                        QString::number(imageSizeInfo_->font().pointSizeF()) +
                        QLatin1Char('|') + settings_.themeMode + QLatin1Char('|') +
                        settings_.accentColor;
    if (key == imageInfoHeightKey_ && imageSizeInfo_->minimumHeight() > 0) return;
    QLabel twin;
    twin.setFont(imageSizeInfo_->font());
    twin.setStyleSheet(imageSizeInfo_->styleSheet());
    twin.ensurePolished();   // the stylesheet's padding counts toward the hint
    twin.setTextFormat(Qt::PlainText);
    twin.setText(QStringLiteral("No image loaded"));
    int h = twin.sizeHint().height();
    twin.setTextFormat(Qt::RichText);
    twin.setText(QStringLiteral("Image Size: 8888 × 8888 px") + incognitoTagHtml());
    h = std::max(h, twin.sizeHint().height());
    imageInfoHeightKey_ = key;
    imageSizeInfo_->setFixedHeight(h);
  }

  void MainWindow::updateImageSizeInfo() {
    QString size;
    if (canvas_ && canvas_->hasImage()) {
      const bool isBlank = !blankColor_.isEmpty();
      size = QString("Image Size: %1 × %2 px%3")
                 .arg(canvas_->imageWidth())
                 .arg(canvas_->imageHeight())
                 .arg(isBlank ? QStringLiteral("  ·  blank") : QString());
    } else {
      size = QStringLiteral("No image loaded");
    }
    if (imageSizeInfo_) {
      // The row's height is RESERVED for the taller of its two states before either is
      // shown, so switching between them cannot resize the info bar (see below).
      reserveImageInfoHeight();
      // Browser parity (drawingApp.js updateInfo + layout.css .info-incognito):
      // the incognito state rides INLINE on this line, accent-coloured and bold,
      // in both the loaded and the empty state. It is our own text, never model
      // output, so rich text is safe here.
      if (incognito_) {
        imageSizeInfo_->setTextFormat(Qt::RichText);
        imageSizeInfo_->setText(size.toHtmlEscaped() + incognitoTagHtml());
      } else {
        imageSizeInfo_->setTextFormat(Qt::PlainText);
        imageSizeInfo_->setText(size);
      }
    }
    // The "?" beside the project name carries the SAME size plus the incognito
    // line — and only those two facts. It is the collapsed state's only readout,
    // so it is refreshed from here (every incognito change ends in this call via
    // updateProjectTitle).
    if (statusHint_) {
      QString tip = size;
      if (incognito_) tip += QStringLiteral("\nIncognito — not saved");
      statusHint_->setToolTip(tip);
      refreshStatusHintVisibility();
    }
  }

  // Mini S-mark logo — a QPainter port of the browser's app-logo SVG (toolbar.js): a dark rounded
  // square with an ACCENT-coloured frame, an inner darker square, and a yellow polyline whose seven
  // dots trace an S. Only the frame tracks the accent (like the browser), so it never looks garish.
  // Repainted on theme/accent change from applyTheme.
  QPixmap MainWindow::makeLogoPixmap(int size) const {
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const double u = size / 64.0;   // browser viewBox is 0..64
    QColor accent = accentPrimary(settings_.accentColor);
    if (!accent.isValid()) accent = QColor("#7c3aed");
    // Outer rounded square (dark), then the accent frame stroke on top.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#2b2f3a"));
    p.drawRoundedRect(QRectF(2 * u, 2 * u, 60 * u, 60 * u), 13 * u, 13 * u);
    QPen frame(accent);
    frame.setWidthF(2.5 * u);
    p.setPen(frame);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2.75 * u, 2.75 * u, 58.5 * u, 58.5 * u), 12.25 * u, 12.25 * u);
    // Inner darker square.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#3a3f4b"));
    p.drawRoundedRect(QRectF(12 * u, 12 * u, 40 * u, 40 * u), 4 * u, 4 * u);
    // Yellow polyline + dots.
    const QPointF pts[7] = {QPointF(44 * u, 20 * u), QPointF(32 * u, 16 * u), QPointF(20 * u, 24 * u),
                            QPointF(32 * u, 32 * u), QPointF(44 * u, 40 * u), QPointF(32 * u, 48 * u),
                            QPointF(20 * u, 44 * u)};
    QPen line(QColor("#FFFF00"));
    line.setWidthF(3.5 * u);
    line.setCapStyle(Qt::RoundCap);
    line.setJoinStyle(Qt::RoundJoin);
    p.setPen(line);
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(pts, 7);
    p.setBrush(QColor("#FFFF00"));
    p.setPen(QPen(QColor("#000000"), 1.25 * u));
    for (const auto& pt : pts) p.drawEllipse(pt, 2.6 * u, 2.6 * u);
    return pm;
  }

  // The logo's accent-preset picker — a FIRST-CLASS popover dialog (not a QMenu),
  // so all Alt-peek/glide/linger/outside-click rules are the popover system's own.
  // Entries come from theme.cpp accentPresets (the one shared list); a pick applies
  // through the click-cycle's exact applySettings(…, true) path, then closes.
  void MainWindow::openAccentPicker() {
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("accentPopover"));
    auto* col = new QVBoxLayout(&dlg);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(1);
    // No section header: the swatches say what this is, and the popover is anchored
    // to the logo that opened it.
    // Rounded colour chip per row — the Settings dropdown's swatch recipe
    // (settingsDialog.cpp), so the two accent pickers read identically. The CURRENT
    // accent's ✓ is baked into its chip (white over a dark halo, readable on light
    // chips), which keeps the rows a tight chip+label pair with no check column.
    const auto swatch = [](const QColor& c, bool current) {
      QPixmap pm(16, 16);
      pm.fill(Qt::transparent);
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing);
      p.setPen(QPen(QColor(0, 0, 0, 70), 1));
      p.setBrush(c);
      p.drawRoundedRect(1, 1, 13, 13, 3, 3);
      if (current) {
        p.setBrush(Qt::NoBrush);
        const QPointF pts[3] = {{4.4, 8.3}, {6.9, 10.7}, {11.4, 5.3}};
        p.setPen(QPen(QColor(0, 0, 0, 160), 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(pts, 3);
        p.setPen(QPen(Qt::white, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(pts, 3);
      }
      p.end();
      return QIcon(pm);
    };
    // Rows are built once and RE-MARKED in place when a pick lands: picking must not
    // rebuild or move the popover (see the clicked handler below).
    QList<QPushButton*> rows;
    for (const AccentPreset& a : accentPresets()) {
      const bool current = a.key == settings_.accentColor;   // a custom #… accent marks nothing
      auto* row = new QPushButton(&dlg);
      row->setObjectName(QStringLiteral("accentRow-") + a.key);
      row->setFlat(true);
      row->setCursor(Qt::PointingHandCursor);
      row->setIconSize(QSize(16, 16));
      row->setIcon(swatch(QColor(a.hex), current));
      row->setText(a.label);
      row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      // Menu-tight metrics: left-aligned label hugging the chip, row-wide hover fill.
      row->setStyleSheet(
          "QPushButton { border: none; border-radius: 6px; text-align: left; "
          "padding: 5px 14px 5px 4px; }"
          "QPushButton:hover { background: palette(highlight); color: palette(highlighted-text); }");
      row->setProperty("accentKey", a.key);         // observable by the GUI test
      row->setProperty("currentAccent", current);
      connect(row, &QPushButton::clicked, &dlg, [this, &dlg, &rows, swatch, key = a.key] {
        auto next = settings_;
        next.accentColor = key;
        applySettings(next, true);   // the click-cycle's apply + persist path
        // The popover STAYS OPEN: the point of the list is trying colours against the
        // live app, so a pick re-marks the ✓ in place and waits for the next one. It
        // closes the ways every popover closes — outside click, Escape, Alt release, a
        // glide to another icon, the app losing focus. Re-marking touches only the row
        // icons, so nothing is rebuilt, re-laid out, or re-anchored under the cursor.
        for (QPushButton* r : rows) {
          const QString rowKey = r->property("accentKey").toString();
          const bool now = rowKey == key;
          r->setIcon(swatch(QColor(accentPrimary(rowKey)), now));
          r->setProperty("currentAccent", now);
        }
        // An accent change re-themes the window (and may play the accent wipe over it):
        // keep the popover on top of whatever that repaints, and keyboard-ready.
        if (popoverOverlay_) popoverOverlay_->raise();
        dlg.setFocus(Qt::PopupFocusReason);
      });
      rows << row;
      col->addWidget(row);
    }
    execMaybePopover(dlg);
  }

  void MainWindow::updateProjectTitle() {
    QString name;
    bool editable = false;
    const bool remote = !remoteSession_->link().id.isEmpty();
    if (incognito_) {
      name = "Incognito";
    } else if (!activeProjectId_.isEmpty()) {
      name = activeProjectName();
      editable = true;   // an active LOCAL project is always renameable/colourable (even if the
                         // registry name lookup momentarily returns empty and we fall back to the id)
    } else if (remote) {
      name = remoteSession_->link().name;   // server-linked session (no local project id)
      editable = true;   // server projects are renameable/colourable too (pushed via commitProjectName)
    }
    if (name.isEmpty() && canvas_ && canvas_->hasImage())
      name = canvas_->imageBaseName();   // show the image name until it's a saved project
    setWindowTitle(name.isEmpty() ? QStringLiteral("Stencil")
                                  : QString("%1 — Stencil").arg(name));
    // Server-editing indicator: a golden frame around the canvas (mirrors the browser
    // badge/outline), so a server-backed session is unmistakable.
    if (scroll_)
      scroll_->setStyleSheet(remote ? "QScrollArea{border:2px solid #d4a017;}"
                                    : QString());
    // Per-project accent: the toolbar name field is painted in the project's colour by
    // applyProjectNameStyle below (empty => theme default). The window title is OS-drawn,
    // so only the field is tinted — mirroring the browser's coloured #project-name-input.
    const bool hasProject = !incognito_ && (!activeProjectId_.isEmpty() || remote);
    // Don't clobber the field while the user is typing in it.
    if (projectName_ && !projectName_->hasFocus()) {
      projectName_->setText(name);
      projectName_->setEnabled(editable);
      projectName_->setReadOnly(true);  // back to read-only after any edit (enter edit via ✎/dbl-click)
      projectName_->setPlaceholderText(
          incognito_ ? QStringLiteral("Incognito (unsaved)") : QStringLiteral("No project"));
      // Custom colour when set; otherwise the shared neutral grey (#80868f), readable on
      // light and dark — mirrors the browser's --project-name-fg (Qt has no text-shadow). The
      // read-only look carries NO border/focus ring (applyProjectNameStyle); the bordered input
      // appears only in edit mode.
      applyProjectNameStyle(false);
      refreshProjectNameButtons();
    }
    // Project-colour menu actions + the toolbar 🎨 icon enable with an active project; the ✎
    // rename pencil only when the name is editable (a saved, non-incognito project).
    if (actProjectColor_) actProjectColor_->setEnabled(hasProject);
    if (actProjectColorClear_) actProjectColorClear_->setEnabled(hasProject);
    if (projectColorBtn_) projectColorBtn_->setEnabled(hasProject);
    if (projectNameEdit_) projectNameEdit_->setEnabled(editable);
    updateImageSizeInfo();
  }

  // Browser-like: the ✓/✗ buttons show only IN edit mode; the ✎ pencil shows only OUT of it.
  // ✓ is enabled only for a changed, valid name (its tooltip carries the reason when disabled).
  void MainWindow::refreshProjectNameButtons() {
    if (!projectName_ || !projectNameAccept_ || !projectNameCancel_) return;
    const bool editable = projectName_->isEnabled();
    // Toggle the QWidgetActions (not the widgets) so the toolbar actually re-lays-out. In edit
    // mode only ✓/✗ show; out of it only ✎ + 🎨 show — exactly like the browser topbar.
    if (projectNameAcceptAction_) projectNameAcceptAction_->setVisible(nameEditing_);
    if (projectNameCancelAction_) projectNameCancelAction_->setVisible(nameEditing_);
    // ✎/🎨 reveal only on name-group hover (✓/✗ replace them while editing) and
    // must not MOVE anything: they keep their slots and are merely painted out —
    // the browser's `visibility: hidden`. Removing slots shoved the "?" sideways.
    const bool affordable = editable && !nameEditing_;
    if (projectNameEditAction_) projectNameEditAction_->setVisible(affordable);
    if (projectColorBtnAction_) projectColorBtnAction_->setVisible(affordable);
    setPaintedOut(projectNameEdit_, affordable && !nameHover_);
    setPaintedOut(projectColorBtn_, affordable && !nameHover_);
    // Blank-colour button: shown only when this session is a blank image (recolourable), regardless
    // of whether it's a saved/editable project (in-memory recolour works for unsaved blanks too).
    // Paint its icon as a live swatch of the current fill colour.
    if (blankColorBtn_) {
      const bool showBlank = !blankColor_.isEmpty() && !nameEditing_;
      blankColorBtn_->setVisible(showBlank);   // now a plain layout widget, gated directly
      if (showBlank) {
        // Same input-palette chip recipe as the line-style colour button
        // (inset swatch rect + luminance-tuned outline, theme/accent tracked).
        const QColor c(blankColor_);
        updateColorSwatch(blankColorBtn_, c.isValid() ? c : QColor("#ffffff"));
      }
    }
    if (!nameEditing_) return;
    const QString v = projectName_->text().trimmed();
    // Compare against the CURRENT name — remoteSession_->link().name for a server-linked session (no local id),
    // else the local name.
    const QString current = !remoteSession_->link().id.isEmpty() ? remoteSession_->link().name : activeProjectName();
    const bool changed = v != current;
    bool ok = changed;
    QString reason = changed ? QStringLiteral("Save name (Enter)") : QStringLiteral("No change");
    if (changed && remoteSession_->link().id.isEmpty()) {
      const auto check = checkProjectName(v, activeProjectId_);
      ok = check.ok;
      if (!ok) reason = QString::fromStdString(check.reason);
    } else if (changed) {  // server project: uniqueness is the server's job
      ok = !v.isEmpty();
      if (!ok) reason = QStringLiteral("Enter a name");
    }
    projectNameAccept_->setEnabled(ok);
    projectNameAccept_->setToolTip(reason);
  }

  // Qt has no `visibility: hidden` — a hidden widget leaves its layout, taking its space with
  // it. An opacity effect paints the widget out while it keeps its slot, which is what the
  // browser's hover-revealed affordances do.
  void MainWindow::setPaintedOut(QWidget* w, bool out) {
    if (!w) return;
    auto* fx = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
    if (!fx) {
      fx = new QGraphicsOpacityEffect(w);
      w->setGraphicsEffect(fx);
    }
    fx->setOpacity(out ? 0.0 : 1.0);
  }

  // Recompute hover state over the name group (field + ✎ + 🎨). Deferred callers give underMouse()
  // a beat to settle after a Leave, so moving the cursor from the field onto ✎ doesn't flicker them.
  void MainWindow::updateNameHover() {
    const bool over = (projectName_ && projectName_->underMouse()) ||
                      (projectNameEdit_ && projectNameEdit_->underMouse()) ||
                      (projectColorBtn_ && projectColorBtn_->underMouse());
    if (over != nameHover_) {
      nameHover_ = over;
      refreshProjectNameButtons();
    }
  }

  // Exactly one override on the stack, ever: push/pop pairs are the whole risk of this
  // approach, so the flag — not the caller — decides whether anything happens.
  void MainWindow::setBlockedCursor(bool on) {
    if (on == blockedCursorOn_) return;
    if (on) QApplication::setOverrideCursor(Qt::ForbiddenCursor);
    else QApplication::restoreOverrideCursor();
    blockedCursorOn_ = on;
  }

  void MainWindow::setActionTip(QAction* a, const QString& desc) {
    if (!a) return;
    const QString sc = a->shortcut().toString(QKeySequence::NativeText);
    a->setToolTip(sc.isEmpty() ? desc : QString("%1 (%2)").arg(desc, sc));
  }

  void MainWindow::enterNameEdit() {
    if (!projectName_ || !projectName_->isEnabled() || nameEditing_) return;
    nameEditing_ = true;
    projectName_->setReadOnly(false);
    applyProjectNameStyle(true);  // show the accent-outlined input look
    projectName_->setFocus();
    projectName_->selectAll();
    refreshProjectNameButtons();  // reveal ✓/✗, hide ✎
  }

  void MainWindow::commitProjectName() {
    const QString newName = projectName_->text().trimmed();
    // Server-linked session (no local id): push the rename straight to the server so peers see it
    // live, version-guarded — mirrors setActiveProjectColor's remote branch. Otherwise rename the
    // local project. (Previously a server project couldn't be renamed at all from the toolbar.)
    if (!remoteSession_->link().id.isEmpty()) {
      stencil::net::ServerClient* c = connections_ ? connections_->find(remoteSession_->link().address) : nullptr;
      if (!newName.isEmpty() && newName != remoteSession_->link().name && c) {
        QPointer<MainWindow> self(this);
        const QString id = remoteSession_->link().id;
        remoteSession_->putVersionGuardedAsync(
            c, id,
            [c, id, newName](qint64 version, std::function<void(bool, qint64, bool)> cb) {
              c->updateProjectNameAsync(id, newName, version, cb);
            },
            [this, self, c, newName](bool ok, qint64 newVersion) {
              if (!self) return;
              if (ok) {
                remoteSession_->link().name = newName;
                remoteSession_->link().version = newVersion;
                notify_->success(QString("Renamed to \"%1\"").arg(newName));
              } else {
                notify_->error(QString("Rename failed: %1").arg(c->lastError()));
              }
              updateProjectTitle();   // reflect the stored name (renamed, or reverted on failure)
            });
      }
    } else if (!activeProjectId_.isEmpty()) {
      renameProjectById(activeProjectId_, projectName_->text());
    }
    nameEditing_ = false;   // leave edit mode → field back to read-only, ✎ returns
    projectName_->clearFocus();
    updateProjectTitle();   // force the field/title back to the stored name
  }

  void MainWindow::cancelProjectName() {
    nameEditing_ = false;   // leave edit mode
    projectName_->clearFocus();
    updateProjectTitle();   // revert the field to the stored name
  }

  // ── Per-project accent colour ──

  QString MainWindow::activeProjectColor() const {
    if (activeProjectId_.isEmpty()) return {};
    for (const auto& p : projectList_)
      if (QString::fromStdString(p.meta.id) == activeProjectId_)
        return QString::fromStdString(p.meta.color);
    return {};
  }

  // The colour of the project this editor is bound to: the linked server record for a
  // server session (no local id), else the active local project. (Does not consider
  // incognito — callers that paint apply that gate themselves.)
  QString MainWindow::currentProjectColor() const {
    return !remoteSession_->link().id.isEmpty() ? remoteSession_->link().color : activeProjectColor();
  }

  std::optional<QString> MainWindow::normalizeProjectColor(const QString& color) const {
    if (color.isEmpty()) return QString();   // explicit clear → theme default
    const QColor c(color);
    if (!c.isValid()) return std::nullopt;   // reject an unparseable colour
    return c.name().toLower();               // canonical "#rrggbb" lower-case
  }

  void MainWindow::chooseProjectColor() {
    // Direct modal picker — identical to the line-colour button, which works cleanly. (Earlier
    // menu/InstantPopup/singleShot variants left a stray mouse grab that closed the dialog.)
    const QString cur = currentProjectColor();
    // No custom colour → seed with the neutral grey the name is actually painted in (the unset
    // default), not the theme accent, so the picker reflects the real current state.
    const QColor seed = (!cur.isEmpty() && QColor(cur).isValid())
                            ? QColor(cur)
                            : QColor("#80868f");
    // Non-native (helper) — the macOS shared NSColorPanel gets dismissed by our event
    // filters; Qt's own modal dialog stays put. Anchored on the 🎨 button that opened it.
    const QColor picked =
        support::pickColorAnimated(seed, this, "Project name color", projectColorBtn_);
    if (!picked.isValid()) return;   // user cancelled
    setActiveProjectColor(picked.name());
  }

  // Browser-style 🎨 popup: a tiny menu rather than opening the picker directly. Always offers
  // "Choose colour…"; offers "Use theme default colour" only when a custom colour is currently set.
  void MainWindow::showProjectColorMenu() {
    const QString cur = currentProjectColor();
    const bool hasCustom = !cur.isEmpty();
    QMenu menu(this);
    QAction* pick = menu.addAction("Choose color…");
    // "Use theme default colour" is only meaningful when a custom colour is set — hide it
    // entirely (not just disable) when the project is already on the theme default.
    QAction* def = hasCustom ? menu.addAction("Use theme default color") : nullptr;
    QAction* chosen =
        menu.exec(projectColorBtn_->mapToGlobal(QPoint(0, projectColorBtn_->height())));
    if (chosen == pick) {
      // Defer so the menu's mouse grab is fully released before the modal picker opens — a live
      // grab is exactly what dismissed the dialog in the earlier direct-popup attempts.
      QTimer::singleShot(0, this, [this] { chooseProjectColor(); });
    } else if (def && chosen == def) {   // guard: dismissed menu yields null, which != def here
      setActiveProjectColor(QString());
    }
  }

  // Paint the name field for its mode. Editing → accent-outlined input (focus ring visible);
  // read-only → a plain title with NO border/focus ring (matches the browser's title look), so a
  // stray single-click focus never shows an editable-looking box. Project colour is kept in both.
  void MainWindow::applyProjectNameStyle(bool editing) {
    if (!projectName_) return;
    const QString color = incognito_ ? QString() : currentProjectColor();
    const QColor c(color);
    // Default (no custom colour): a brighter grey than the browser's #80868f + bold, since Qt can't
    // give a QLineEdit the browser's legibility text-shadow — bold + a lighter grey matches the
    // perceived brightness. A custom colour is used as-is (also bold).
    const QString fg =
        (!color.isEmpty() && c.isValid()) ? c.name() : QStringLiteral("#9aa0a8");
    if (editing) {
      const QColor accent = accentPrimary(settings_.accentColor);
      projectName_->setStyleSheet(
          QString("QLineEdit{color:%1;font-weight:600;border:1px solid %2;border-radius:6px;"
                  "background:palette(base);padding:2px 6px;}"
                  "QLineEdit:focus{border:1px solid %2;}")
              .arg(fg, accent.name()));
    } else {
      projectName_->setStyleSheet(
          QString("QLineEdit{color:%1;font-weight:600;border:1px solid transparent;background:transparent;}"
                  "QLineEdit:focus{border:1px solid transparent;}")
              .arg(fg));
    }
  }

  void MainWindow::setActiveProjectColor(const QString& color) {
    const auto norm = normalizeProjectColor(color);
    if (!norm) {
      notify_->error("Invalid color");
      return;
    }
    // A server-linked session has no local id: push the colour straight to the server.
    if (!remoteSession_->link().id.isEmpty()) {
      const QString n = *norm;
      QPointer<MainWindow> self(this);
      setProjectColorById(remoteSession_->link().id, remoteSession_->link().address, n,
                          [this, self, n](bool ok) {
                            if (!self || !ok) return;
                            remoteSession_->link().color = n;
                            updateProjectTitle();
                          });
      return;
    }
    if (activeProjectId_.isEmpty()) {
      notify_->info("Open or save a project first");
      return;
    }
    QPointer<MainWindow> self(this);
    setProjectColorById(activeProjectId_, QString(), *norm,
                        [this, self](bool ok) { if (self && ok) updateProjectTitle(); });
  }

  void MainWindow::setActiveBlankColor() {
    if (blankColor_.isEmpty() || !canvas_->hasImage()) return;  // blanks only
    QColor init(blankColor_);
    if (!init.isValid()) init = QColor("#ffffff");
    // Qt's own dialog (not the OS-native one), anchored on the Blank swatch button.
    const QColor c =
        support::pickColorAnimated(init, this, "Blank background color", blankColorBtn_);
    if (!c.isValid()) return;
    applyBlankColor(c);
  }

  // The recolour itself, dialog-free — shared by the toolbar button above and
  // the assistant's §10 blankColor op (ChatPlanTarget).
  void MainWindow::applyBlankColor(const QColor& c) {
    if (blankColor_.isEmpty() || !canvas_->hasImage() || !c.isValid()) return;  // blanks only
    // Regenerate the solid fill at the current size, KEEPING the drawn lines (a separate overlay).
    const core::Lines keep = canvas_->lines();
    QImage img(canvas_->imageWidth(), canvas_->imageHeight(), QImage::Format_RGB32);
    img.fill(c);
    canvas_->loadFromImage(img);
    setSourceBytes({}, {});  // recoloured blank is synthetic → re-encode on bundle
    if (!keep.empty()) canvas_->setLines(keep);
    blankColor_ = c.name();
    canvas_->setBlankPage(true);  // loadFromImage reset the flag; still a blank
    // Persist the new fill into the active local project's meta + raster so a reopen shows it.
    // (A server-linked session pushes the recoloured original on the next Save.)
    if (Project* pr = findProject(activeProjectId_.toStdString())) {
      pr->meta.blankColor = blankColor_.toStdString();
      pr->meta.blank = true;
      if (!pr->imagePath.isEmpty()) canvas_->originalImage().save(pr->imagePath, "PNG");
      fileStore::saveProjects(projectList_);
    }
    refreshActions();
  }

  void MainWindow::setProjectColorById(const QString& id, const QString& serverUrl,
                                       const QString& color, std::function<void(bool)> done) {
    const auto norm = normalizeProjectColor(color);
    if (!norm) {
      notify_->error("Invalid color");
      if (done) done(false);
      return;
    }
    // Server project: version-guarded PUT UpdateProject{color} (async). Refresh our linked
    // version when it's the open session so a later save doesn't 409.
    if (!serverUrl.isEmpty()) {
      stencil::net::ServerClient* c = remoteSession_->requireClient(serverUrl);
      if (!c) { if (done) done(false); return; }
      const QString n = *norm;
      QPointer<MainWindow> self(this);
      remoteSession_->putVersionGuardedAsync(
          c, id,
          [c, id, n](qint64 version, std::function<void(bool, qint64, bool)> cb) {
            c->updateProjectColorAsync(id, n, version, cb);
          },
          [this, self, c, id, serverUrl, n, done](bool ok, qint64 newVersion) {
            if (!self) return;
            if (!ok) {
              notify_->error(QString("Color update failed: %1").arg(c->lastError()));
              if (done) done(false);
              return;
            }
            if (remoteSession_->link().id == id && remoteSession_->link().address == serverUrl)
              remoteSession_->link().version = newVersion;
            notify_->success(n.isEmpty() ? QStringLiteral("Color reset to theme default")
                                         : QString("Color set to %1").arg(n));
            if (done) done(true);
          });
      return;
    }
    // Local project: update the meta + persist (synchronous).
    Project* pr = findProject(id.toStdString());
    if (!pr) { if (done) done(false); return; }
    pr->meta.color = norm->toStdString();
    fileStore::saveProjects(projectList_);
    refreshDockMenu();
    notify_->success(norm->isEmpty() ? QStringLiteral("Color reset to theme default")
                                     : QString("Color set to %1").arg(*norm));
    if (done) done(true);
  }

  void MainWindow::openInfo() {
    InfoDialog dlg(this);
    execMaybePopover(dlg, actInfo_);   // the controls/shortcuts window grows out of its icon too
  }

  // S13: open the rebind dialog, then persist overrides and re-apply them to the
  // live QActions without a restart.
  void MainWindow::openShortcuts() {
    QVector<ShortcutsDialog::Entry> entries;
    for (auto it = hotkeyDefaults_.begin(); it != hotkeyDefaults_.end(); ++it) {
      ShortcutsDialog::Entry e;
      e.id = it.key();
      e.label = hotkeyLabels_.value(it.key());
      e.defaultSeq = it.value();
      e.currentSeq = hotkeys_.value(it.key(), it.value());
      entries.push_back(e);
    }
    ShortcutsDialog dlg(entries, this);
    if (execMaybePopover(dlg, actShortcuts_) != QDialog::Accepted) return;   // grows out of its icon too

    const auto overrides = dlg.overrides();
    // Rebuild the effective map: defaults, then overrides on top.
    hotkeys_ = hotkeyDefaults_;
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
      hotkeys_.insert(it.key(), it.value());
    if (!incognito_) fileStore::saveHotkeys(overrides);  // incognito suppresses

    // Warn (but still apply) if two distinct ids now resolve to the same
    // non-empty sequence — mirrors the browser's duplicate-binding caution.
    // Sequences are normalized to PortableText so equivalent spellings collide.
    QHash<QString, QString> seen;  // normalized seq -> first id using it
    for (auto it = hotkeys_.begin(); it != hotkeys_.end(); ++it) {
      const QString seq =
          QKeySequence(it.value()).toString(QKeySequence::PortableText);
      if (seq.isEmpty()) continue;  // unset bindings are never duplicates
      const auto prior = seen.constFind(seq);
      if (prior != seen.constEnd()) {
        auto label = [this](const QString& id) {
          const QString l = hotkeyLabels_.value(id);
          return l.isEmpty() ? id : l;
        };
        // Notifications has Info/Success/Error levels; Error is the strongest
        // visual cue for this caution. Still applies below (warn, don't block).
        // Comparison stays PortableText (above); only the shown seq is native.
        const QString shown = QKeySequence(seq).toString(QKeySequence::NativeText);
        notify_->error(QString("Duplicate shortcut: '%1' is bound to %2 and %3")
                           .arg(shown, label(prior.value()), label(it.key())));
        break;  // one warning is enough; still applies below
      }
      seen.insert(seq, it.key());
    }

    // Re-apply to the live actions. platformizeSeq keeps the delete combos on the
    // macOS Backspace key (no-op for everything else / off macOS).
    for (auto it = hotkeyActions_.begin(); it != hotkeyActions_.end(); ++it) {
      const QString seq = hotkeys_.value(it.key(), hotkeyDefaults_.value(it.key()));
      it.value()->setShortcut(QKeySequence(platformizeSeq(seq)));
    }
  }

  void MainWindow::updateStatusIdle() {
    // The coordinate bar reads out the cursor and nothing else: empty with the pointer off
    // the canvas, and empty when there is no image at all. It used to carry an "Open an
    // image…" invitation — the canvas already shows one in the middle of the empty page,
    // and the browser's bar is blank there too.
    status_->setText(QString());
  }

}
