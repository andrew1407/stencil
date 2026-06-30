#include "mainWindow.hpp"
#include "blankImageDialog.hpp"
#include "openImageDialog.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "incognitoOverlay.hpp"
#include "cropGeometry.hpp"
#include "geometry.hpp"
#include "pageMetrics.hpp"
#include "cropDialog.hpp"
#include "tooltipRows.hpp"
#include "zoomPan.hpp"
#include "guiHelpers.hpp"
#include "iconSet.hpp"
#include "infoDialog.hpp"
#include "launchOptions.hpp"
#include "linksDialog.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "connectionStore.hpp"
#include "liveFeed.hpp"
#include "serverClient.hpp"
#include "selectionPanel.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "theme.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QBuffer>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QEasingCurve>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QSet>
#include <QUrl>
#include <QStyleHints>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPalette>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QShortcut>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
#include <algorithm>

namespace stencil::gui {

  namespace {
    long long nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

    // On macOS the primary delete key emits Backspace (⌫), so the shared
    // "Alt+Delete" defaults must bind to Backspace to fire on the key Mac users
    // actually press (mirrors the browser's platformizeCombo Delete→Backspace).
    // No-op for other combos and off macOS.
    QString platformizeSeq(QString seq) {
#ifdef Q_OS_MACOS
      seq.replace(QStringLiteral("Delete"), QStringLiteral("Backspace"),
                  Qt::CaseInsensitive);
#endif
      return seq;
    }

    std::string makeSalt() {
      return QString::number(QRandomGenerator::global()->bounded(1 << 24), 36)
          .toStdString();
    }

    // Natural page dimensions (cm) as selected — NOT orientation-swapped (only
    // the proportions matter for the crop aspect). Mirrors the browser
    // cropModal.pageDims helper.
    core::PageSize naturalPageCm(const QString& pageSize, double customW,
                                 double customH) {
      if (pageSize == "custom") return {customW, customH};
      const core::PageSize ps = core::namedPageSize(pageSize.toStdString());
      return ps.width > 0 ? ps : core::namedPageSize("A4");
    }

    // Encode a QImage as PNG bytes for upload (the server is codec-free, so the
    // desktop hands it already-encoded image bytes + the dimensions separately).
    QByteArray pngBytes(const QImage& img) {
      QByteArray out;
      QBuffer buf(&out);
      buf.open(QIODevice::WriteOnly);
      img.save(&buf, "PNG");
      return out;
    }
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

    selPanel_ = new SelectionPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, selPanel_);

    notify_ = new Notifications(scroll_->viewport());
    // Incognito indicator (dashed frame + badge) pinned to the canvas viewport,
    // mirroring the browser's body.incognito-mode outline/badge. Hidden until the
    // incognito action toggles it on.
    incognitoOverlay_ = new IncognitoOverlay(scroll_->viewport());
    tooltip_ = new CanvasTooltip(this);  // floating hover tooltip (S12)

    status_ = new QLabel("Open an image — or create a blank one — to begin", this);
    statusBar()->addWidget(status_);

    pageSize_ = new QComboBox(this);
    pageSize_->addItems({"A3", "A4", "custom"});  // S10 custom page
    pageSize_->setToolTip("Page format used for cm/inch measurements (A3, A4, or custom)");
    zoom_ = new QComboBox(this);
    zoom_->addItems({"25%", "50%", "75%", "100%", "150%", "200%", "400%"});
    zoom_->setToolTip("Zoom level — pick a preset or type an exact percent");
    // Editable so the user can type an exact percent, but NoInsert so reflecting
    // a programmatic zoom (Ctrl+wheel) never appends list items — mirrors browser
    // zoomPan.js setZoom (a clamped numeric percent, never an accumulating list).
    zoom_->setEditable(true);
    zoom_->setInsertPolicy(QComboBox::NoInsert);
    zoom_->setCurrentText("100%");

    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setSingleShot(true);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::saveSessionNow);

    // Live co-edit: debounced push of local edits + periodic poll for peer changes.
    remotePushTimer_ = new QTimer(this);
    remotePushTimer_->setSingleShot(true);
    connect(remotePushTimer_, &QTimer::timeout, this, [this] {
      remotePushBurstStart_ = 0;   // burst flushed — start a fresh max-wait window next edit
      if (!remoteAddress_.isEmpty()) saveToServer();
    });
    remotePollTimer_ = new QTimer(this);
    remotePollTimer_->setInterval(2000);   // backstop behind the live push feed
    connect(remotePollTimer_, &QTimer::timeout, this, &MainWindow::pollRemoteForUpdate);
    // Coalesce a burst of live-feed events into one reload, and run it off the socket
    // read slot (openServerProject spins a nested event loop, so a direct call would
    // re-enter). Re-checked against remoteVersion_ at fire time.
    remoteReloadTimer_ = new QTimer(this);
    remoteReloadTimer_->setSingleShot(true);
    connect(remoteReloadTimer_, &QTimer::timeout, this, [this] {
      if (remoteAddress_.isEmpty() || remoteId_.isEmpty()) return;
      if (!settings_.syncToServer) return;
      if (remoteReloading_) { remoteReloadPending_ = true; return; }  // nested-loop guard
      // A local edit is pending/in-flight — don't clobber it; retry shortly (our push wins
      // last-writer-wins, then we reload the merged result).
      if (remotePushing_ || (remotePushTimer_ && remotePushTimer_->isActive())) {
        remoteReloadPending_ = true;
        remoteReloadTimer_->start(150);
        return;
      }
      remoteReloadPending_ = false;
      openServerProject(remoteAddress_, remoteId_, /*silent=*/true);
      // Events that landed during the reload's nested event loop queued a pending flag —
      // converge to the latest with one more pass.
      if (remoteReloadPending_) {
        remoteReloadPending_ = false;
        remoteReloadTimer_->start(40);
      }
    });

    buildActions();
    buildContextActions();  // S11: nested context-menu submenu actions
    buildMenus();
    buildToolbar();

    // ── wiring ── (after buildToolbar so the referenced widgets/actions exist)
    wireSignals();

    // ── load persisted state ──
    projectList_ = fileStore::loadProjects();
    settings_ = fileStore::loadSettings();
    applySettings(settings_, false);
    if (restoreLast) restoreSession();  // skipped for a blank incognito editor
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
    const auto overrides = fileStore::loadHotkeys();
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
      hotkeys_.insert(it.key(), it.value());
  }

  // Signal wiring extracted from the ctor. The connect() ORDER is observable
  // (e.g. the allowFormulas_ handler drives actAllowFormulas_; customW_/customH_
  // handlers call onSelectionChanged) and is preserved verbatim here. Must run
  // after the widgets/actions are built and before the persisted-state load.
  void MainWindow::wireSignals() {
    connect(canvas_, &CanvasWidget::hovered, this, &MainWindow::onHovered);
    connect(canvas_, &CanvasWidget::changed, this, &MainWindow::onCanvasChanged);
    connect(canvas_, &CanvasWidget::selectionChanged, this,
            &MainWindow::onSelectionChanged);
    connect(canvas_, &CanvasWidget::contextRequested, this,
            &MainWindow::showContextMenu);
    // Idle-canvas click (no image yet) opens the blank-image creator.
    connect(canvas_, &CanvasWidget::blankImageRequested, this,
            &MainWindow::newBlankImage);
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
    // Page size + custom inputs (S10).
    connect(pageSize_, &QComboBox::currentTextChanged, this,
            [this](const QString&) { onPageSizeChanged(); });
    connect(customW_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
              // Spinboxes are edited in the active unit; store the model in cm.
              settings_.customPageWidth = v / unitFormat().factor;
              persistSettings();
              onHovered(lastHoverX_, lastHoverY_);
              onSelectionChanged();  // refresh panel cm (S10/GAP-2)
              scheduleRemotePush();  // page format rides the layout — push it to peers
            });
    connect(customH_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
              settings_.customPageHeight = v / unitFormat().factor;
              persistSettings();
              onHovered(lastHoverX_, lastHoverY_);
              onSelectionChanged();  // refresh panel cm (S10/GAP-2)
              scheduleRemotePush();
            });
    // Formula controls (S11). The toolbar checkbox is the single source of
    // truth; the View ▸ Allow Formulas action just drives it (and is kept in
    // sync here), so the feature stays reachable when the toolbar overflows.
    connect(allowFormulas_, &QCheckBox::toggled, this, [this](bool on) {
      settings_.allowFormulas = on;
      if (formulaGroupAct_) formulaGroupAct_->setVisible(on);
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
      scheduleRemotePush();  // formulas ride the layout — push to peers
    });
    connect(actAllowFormulas_, &QAction::toggled, this,
            [this](bool on) { allowFormulas_->setChecked(on); });
    connect(formulaX_, &QLineEdit::textChanged, this,
            [this](const QString&) { validateAndApplyFormulas(); });
    connect(formulaY_, &QLineEdit::textChanged, this,
            [this](const QString&) { validateAndApplyFormulas(); });
    connect(selPanel_, &SelectionPanel::pointActivated, this,
            [this](int i) { canvas_->selectPoint(i); });
    connect(selPanel_, &SelectionPanel::pointDeleteRequested, this,
            [this](int i) { canvas_->deletePoint(i); });
    connect(selPanel_, &SelectionPanel::pointCoordChanged, this,
            [this](int i, int axis, double v) { canvas_->setPointCoord(i, axis, v); });

    // ── selection-panel inline line editor → canvas mutators (Step 10).
    // Mirrors browser/js/core/drawingApp.js:181-195 applySelectionChange /
    // applyFill / deselectLine wiring; the SelectionPanel owns inline line
    // editing (PARITY_PLAN3 conflict-resolution: toolbar sets defaults only).
    connect(selPanel_, &SelectionPanel::lineColorChanged, this,
            [this](const QString& c) { canvas_->setSelectedLineColor(c); });
    connect(selPanel_, &SelectionPanel::lineThicknessChanged, this,
            [this](int t) { canvas_->setSelectedLineThickness(t); });
    connect(selPanel_, &SelectionPanel::lineMarkerSizeChanged, this,
            [this](int m) { canvas_->setSelectedLineMarker(m); });
    connect(selPanel_, &SelectionPanel::lineStyleChanged, this,
            [this](const QString& s) { canvas_->setSelectedLineStyle(s); });
    connect(selPanel_, &SelectionPanel::lineFillChanged, this,
            [this](const QString& f) { canvas_->setSelectedLineFill(f); });
    connect(selPanel_, &SelectionPanel::lineDeleteRequested, this,
            [this] { canvas_->deleteSelectedLine(); });
    connect(selPanel_, &SelectionPanel::deselectRequested, this,
            [this] { canvas_->deselect(); });
  }

  QString MainWindow::hotkey(const QString& id, const QString& fallback) const {
    return hotkeys_.value(id, fallback);
  }

  void MainWindow::buildActions() {
    auto mk = [this](const QString& text, const QString& seq) {
      auto* a = new QAction(text, this);
      if (!seq.isEmpty()) a->setShortcut(QKeySequence(seq));
      // WindowShortcut (default): fires when the main window is active, but not
      // over modal dialogs — so Backspace/Esc stay usable inside dialogs.
      // Show the shortcut natively (⌘C on macOS, Ctrl+C elsewhere); storage and
      // matching keep the portable form via QKeySequence above.
      const QString shown =
          QKeySequence(seq).toString(QKeySequence::NativeText);
      a->setToolTip(seq.isEmpty() ? text : QString("%1 (%2)").arg(text, shown));
      addAction(a);  // register the shortcut on the window
      return a;
    };

    // Richer tooltip than mk()'s default while KEEPING the hotkey suffix (browser
    // composeControlTitle). Matters on the icon-only toolbar, where the tooltip is
    // the only place the hotkey shows.
    auto tip = [](QAction* a, const QString& desc) {
      const QString sc = a->shortcut().toString(QKeySequence::NativeText);
      a->setToolTip(sc.isEmpty() ? desc : QString("%1 (%2)").arg(desc, sc));
    };

    actOpen_ = mk("Open Image…", hotkey("loadImage", "Ctrl+O"));
    actOpenAnother_ = mk("Open Another Image…",
                         hotkey("openAnotherImage", "Ctrl+Shift+O"));
    tip(actOpenAnother_,
        "Open another image — replace the current editor or launch it in a new window");
    // Emoji prefixes were removed from these labels now that every action carries
    // a themed line-art icon (styleActionIcons): the menu shows icon + clean text,
    // the icon-only toolbar shows the glyph with the label on its tooltip.
    // New Blank / New Line have no entry in the shared hotkeysConfig.json
    // registry, so their (browser-coordinated) defaults are set literally here
    // rather than through hotkey(); they're not listed in the Customize dialog.
    actNewBlank_ = mk("New Blank Image…", "Ctrl+Shift+B");
    tip(actNewBlank_,
        "Create a blank image (white, black, or any color) to draw on");
    actCrop_ = mk("Crop Image…", hotkey("cropImage", "Ctrl+Shift+X"));
    tip(actCrop_,
        "Crop the image — pick the page-shaped region to show on the canvas");
    // Non-destructive 90° rotation (browser hotkeys rotateImageLeft=Alt+R,
    // rotateImageRight=Alt+Shift+R). The crop window and lines follow the picture.
    actRotateLeft_ = mk("Rotate Left", hotkey("rotateImageLeft", "Alt+R"));
    tip(actRotateLeft_, "Rotate the image left (counter-clockwise)");
    actRotateRight_ = mk("Rotate Right", hotkey("rotateImageRight", "Alt+Shift+R"));
    tip(actRotateRight_, "Rotate the image right (clockwise)");
    actCycleFilter_ = mk("Cycle Image Filter", hotkey("cycleFilter", "Alt+B"));
    tip(actCycleFilter_, "Cycle the image filter (none → B&W → sepia → tint)");
    // Start/Stop drawing (S5): mirrors hotkeysConfig startDraw=Alt+A,
    // stopDraw=Alt+S. actNewLine_ keeps "commit + begin a fresh line" but loses
    // its shortcut to avoid colliding with Stop (Alt+S now drives stopDraw).
    actStartDraw_ = mk("Start Drawing", hotkey("startDraw", "Alt+A"));
    actStopDraw_ = mk("Stop Drawing", hotkey("stopDraw", "Alt+S"));
    actNewLine_ = mk("New Line", "Alt+N");
    actUndo_ = mk("Undo", hotkey("undo", "Ctrl+Z"));
    actRedo_ = mk("Redo", hotkey("redo", "Ctrl+Shift+Z"));
    actDeleteLast_ = mk("Delete Last Point", "Backspace");
    // Selection deletes (shared hotkeysConfig deleteLine=Alt+Delete,
    // deletePoint=Alt+Shift+Delete). On macOS Delete→Backspace so ⌥⌫ / ⌥⇧⌫ work.
    actDeleteLine_ =
        mk("Delete Selected Line", platformizeSeq(hotkey("deleteLine", "Alt+Delete")));
    actDeletePoint_ = mk("Delete Selected Point",
                         platformizeSeq(hotkey("deletePoint", "Alt+Shift+Delete")));
    actClearAll_ = mk("Clear All Lines", hotkey("clearAllLines", "Alt+W"));
    actDeselect_ = mk("Deselect", "Esc");
    actZoomIn_ = mk("Zoom In", hotkey("zoomIn", "Alt+Up"));
    actZoomOut_ = mk("Zoom Out", hotkey("zoomOut", "Alt+Down"));
    actFit_ = mk("Fit to Window", hotkey("resetZoom", "Alt+0"));
    actShowPoints_ = mk("Show Points", hotkey("togglePoints", "Alt+P"));
    actShowLines_ = mk("Show Lines", hotkey("toggleLines", "Alt+L"));
    actTheme_ = mk("Dark Theme", hotkey("toggleTheme", "Ctrl+D"));
    actPanel_ = mk("Selection Panel", hotkey("togglePointsList", "Alt+X"));
    actFullscreen_ = mk("Fullscreen", hotkey("fullscreen", "Alt+F"));
    actSettings_ = mk("Settings…", "Ctrl+,");
    actProjects_ = mk("Projects…", hotkey("openProjects", "Ctrl+Shift+P"));
    actConnect_ = mk("Servers…", hotkey("openServers", "Ctrl+Shift+K"));
    tip(actConnect_,
        "Connect to collaboration servers — shared projects appear with a golden outline");
    actLinks_ = mk("Image Links…", hotkey("openLinks", "Ctrl+Shift+L"));
    // New Project has no shared hotkeysConfig.json entry; literal default here.
    actNewProject_ = mk("New Project", "Ctrl+Shift+N");
    actSaveProject_ = mk("Save to Project", "Ctrl+Shift+S");
    actSaveSession_ = mk("Save Session", "Ctrl+S");
    actInfo_ = mk("Info && Shortcuts", hotkey("openHelp", "F1"));
    actIncognito_ = mk("Incognito", hotkey("toggleIncognito", "Ctrl+Shift+G"));
    actTooltip_ = mk("Hover Tooltip", QString());
    actTooltip_->setCheckable(true);
    // Allow-formulas toggle (S11), also reachable from the View menu so the
    // f(x,y) inputs aren't lost when the toolbar overflows. Two-way synced with
    // the toolbar allowFormulas_ checkbox below.
    actAllowFormulas_ = mk("Allow Formulas", QString());
    actAllowFormulas_->setCheckable(true);
    actQuit_ = mk("Quit", "Ctrl+Q");

    // Data actions (S9). The clipboard hotkeys come from hotkeysConfig.json
    // (copyImage=Ctrl+C, copyLayout=Alt+J, paste=Ctrl+V) so a rebind re-applies
    // live; the JSON file export/import are menu-only (no browser hotkey).
    actDownloadJson_ = mk("Export Layout JSON…", hotkey("downloadJson", "Ctrl+Shift+J"));
    actUploadJson_ = mk("Import Layout JSON…", hotkey("uploadJson", "Ctrl+Shift+U"));
    actCopyLayout_ = mk("Copy Layout JSON", hotkey("copyLayout", "Ctrl+Alt+C"));
    actPasteLayout_ = mk("Paste Layout JSON", QString());
    actSaveImage_ = mk("Save Image…", hotkey("saveImage", "Ctrl+Shift+D"));
    actCopyImage_ = mk("Copy Image to Clipboard", hotkey("copyImage", "Ctrl+C"));
    // Single Ctrl+V entrypoint (paste hotkey): image takes priority over a layout
    // JSON text payload, mirroring the browser paste listener (drawingApp.js
    // :563-591). pasteImage() does that dispatch.
    actPasteImage_ = mk("Paste (Image or Layout)", hotkey("paste", "Ctrl+V"));

    connect(actDownloadJson_, &QAction::triggered, this,
            &MainWindow::downloadLayout);
    connect(actUploadJson_, &QAction::triggered, this, &MainWindow::uploadLayout);
    connect(actCopyLayout_, &QAction::triggered, this, &MainWindow::copyLayout);
    connect(actPasteLayout_, &QAction::triggered, this, &MainWindow::pasteLayout);
    connect(actSaveImage_, &QAction::triggered, this, &MainWindow::saveImageFile);
    connect(actCopyImage_, &QAction::triggered, this,
            &MainWindow::copyImageToClipboard);
    connect(actPasteImage_, &QAction::triggered, this, &MainWindow::pasteImage);

    // Incognito (S6): edit without saving. Togglable only before an image is
    // loaded (browser behavior), so it gets disabled once content exists.
    actIncognito_->setCheckable(true);
    actIncognito_->setToolTip(
        "Incognito — edit without saving (choose before adding an image)");

    actShowPoints_->setCheckable(true);
    actShowLines_->setCheckable(true);
    actPanel_->setCheckable(true);
    actPanel_->setChecked(true);

    connect(actOpen_, &QAction::triggered, this, &MainWindow::openImage);
    connect(actOpenAnother_, &QAction::triggered, this,
            &MainWindow::openAnotherImage);
    connect(actNewBlank_, &QAction::triggered, this, &MainWindow::newBlankImage);
    connect(actCrop_, &QAction::triggered, this, &MainWindow::openCropDialog);
    auto rotate = [this](bool clockwise) {
      if (!canvas_->hasImage()) {
        notify_->error("Open an image first");
        return;
      }
      canvas_->rotateImage(clockwise);
      fitToWindow();
      refreshActions();
    };
    connect(actRotateLeft_, &QAction::triggered, this, [rotate] { rotate(false); });
    connect(actRotateRight_, &QAction::triggered, this, [rotate] { rotate(true); });
    // Cycle the image filter (Alt+B) — mirrors the browser's cycleFilter hotkey:
    // none → bw → sepia → custom(tint). applyImageFilter marks it dirty + syncs.
    connect(actCycleFilter_, &QAction::triggered, this, [this] {
      if (!canvas_->hasImage()) return;
      static const QStringList order{"none", "bw", "sepia", "custom"};
      const int cur = order.indexOf(settings_.imageFilter);
      applyImageFilter(order[(cur + 1) % order.size()]);
    });
    connect(actStartDraw_, &QAction::triggered, canvas_,
            &CanvasWidget::startDrawingMode);
    connect(actStopDraw_, &QAction::triggered, canvas_,
            &CanvasWidget::stopDrawingMode);
    connect(actNewLine_, &QAction::triggered, canvas_, &CanvasWidget::startNewLine);
    connect(actUndo_, &QAction::triggered, canvas_, &CanvasWidget::undo);
    connect(actRedo_, &QAction::triggered, canvas_, &CanvasWidget::redo);
    connect(actDeleteLast_, &QAction::triggered, canvas_,
            &CanvasWidget::deleteLastPoint);
    connect(actDeleteLine_, &QAction::triggered, canvas_,
            &CanvasWidget::deleteSelectedLine);
    connect(actDeletePoint_, &QAction::triggered, this,
            [this] { canvas_->deletePoint(canvas_->selectedPoint()); });
    connect(actClearAll_, &QAction::triggered, canvas_, &CanvasWidget::clearAll);
    connect(actDeselect_, &QAction::triggered, canvas_, &CanvasWidget::deselect);
    connect(actZoomIn_, &QAction::triggered, this, &MainWindow::zoomIn);
    connect(actZoomOut_, &QAction::triggered, this, &MainWindow::zoomOut);
    connect(actFit_, &QAction::triggered, this, &MainWindow::fitToWindow);
    connect(actShowPoints_, &QAction::toggled, this, [this](bool on) {
      canvas_->setShowPoints(on);
      settings_.showPoints = on;
      fileStore::saveSettings(settings_);
    });
    connect(actShowLines_, &QAction::toggled, this, [this](bool on) {
      canvas_->setShowLines(on);
      settings_.showLines = on;
      fileStore::saveSettings(settings_);
    });
    connect(actTheme_, &QAction::triggered, this, &MainWindow::toggleTheme);
    connect(actPanel_, &QAction::toggled, selPanel_, &QWidget::setVisible);
    connect(actFullscreen_, &QAction::triggered, this,
            &MainWindow::toggleFullscreen);
    connect(actSettings_, &QAction::triggered, this, &MainWindow::openSettings);
    connect(actProjects_, &QAction::triggered, this, &MainWindow::openProjects);
    connect(actConnect_, &QAction::triggered, this, &MainWindow::openConnections);
    connect(actLinks_, &QAction::triggered, this, &MainWindow::openLinks);
    connect(actNewProject_, &QAction::triggered, this,
            &MainWindow::newProjectFromCanvas);
    connect(actSaveProject_, &QAction::triggered, this,
            &MainWindow::saveToActiveProject);
    connect(actSaveSession_, &QAction::triggered, this, [this] {
      saveSessionNow();
      notify_->success("Session saved");
    });
    actShortcuts_ = mk("Customize Shortcuts…", QString());
    connect(actShortcuts_, &QAction::triggered, this,
            &MainWindow::openShortcuts);

    // Map hotkey ids -> their actions so a rebind can re-apply live (S13). Only
    // ids present in hotkeysConfig.json are rebindable.
    hotkeyActions_["rotateImageLeft"] = actRotateLeft_;
    hotkeyActions_["rotateImageRight"] = actRotateRight_;
    hotkeyActions_["startDraw"] = actStartDraw_;
    hotkeyActions_["stopDraw"] = actStopDraw_;
    hotkeyActions_["clearAllLines"] = actClearAll_;
    hotkeyActions_["deleteLine"] = actDeleteLine_;
    hotkeyActions_["deletePoint"] = actDeletePoint_;
    hotkeyActions_["togglePoints"] = actShowPoints_;
    hotkeyActions_["toggleLines"] = actShowLines_;
    hotkeyActions_["togglePointsList"] = actPanel_;
    hotkeyActions_["fullscreen"] = actFullscreen_;
    hotkeyActions_["resetZoom"] = actFit_;
    hotkeyActions_["zoomIn"] = actZoomIn_;
    hotkeyActions_["zoomOut"] = actZoomOut_;
    hotkeyActions_["undo"] = actUndo_;
    hotkeyActions_["redo"] = actRedo_;
    // Data clipboard hotkeys (S9; hotkeysConfig.json copyImage/copyLayout/paste).
    hotkeyActions_["copyImage"] = actCopyImage_;
    hotkeyActions_["copyLayout"] = actCopyLayout_;
    hotkeyActions_["paste"] = actPasteImage_;
    // File / project hotkeys whose defaults live in the shared hotkeysConfig.json
    // (coordinated with the browser), wired so a rebind re-applies live and they
    // appear in the Customize Shortcuts dialog.
    hotkeyActions_["openAnotherImage"] = actOpenAnother_;
    hotkeyActions_["cropImage"] = actCrop_;
    hotkeyActions_["saveImage"] = actSaveImage_;
    hotkeyActions_["downloadJson"] = actDownloadJson_;
    hotkeyActions_["uploadJson"] = actUploadJson_;
    hotkeyActions_["openServers"] = actConnect_;
    hotkeyActions_["openLinks"] = actLinks_;
    hotkeyActions_["toggleIncognito"] = actIncognito_;
    hotkeyActions_["loadImage"] = actOpen_;
    hotkeyActions_["openProjects"] = actProjects_;
    hotkeyActions_["toggleTheme"] = actTheme_;
    hotkeyActions_["openHelp"] = actInfo_;

    connect(actInfo_, &QAction::triggered, this, &MainWindow::openInfo);
    connect(actIncognito_, &QAction::toggled, this, [this](bool on) {
      incognito_ = on;
      incognitoOverlay_->setActive(on);
      notify_->info(on ? "Incognito mode — this editor won't be saved"
                       : "Incognito off");
      updateProjectTitle();
    });
    connect(actTooltip_, &QAction::toggled, this, [this](bool on) {
      settings_.tooltipEnabled = on;
      if (!on) tooltip_->hide();
      persistSettings();
    });
    connect(actQuit_, &QAction::triggered, this, &QWidget::close);
  }

  // Persistent context-menu submenu actions (S11). Port of the wiring done once
  // in browser/js/ui/contextMenu.js wire() (~112-605): the draw-mode bridge, the
  // instant-rectangle item, and the Style / Image-Filter / Tooltip submenus.
  // Built once and reused on every right-click; showContextMenu() only re-syncs
  // their checked/enabled/visible state before exec (mirroring syncState ~239).
  void MainWindow::buildContextActions() {
    // ── Draw-mode bridge (contextMenu.js:416-421). Flip line<->rect on the
    // canvas, persist, and notify. The label is re-synced in showContextMenu.
    actDrawModeToggle_ = new QAction("Switch to Rectangle Drawing", this);
    connect(actDrawModeToggle_, &QAction::triggered, this, [this] {
      const bool toRect = canvas_->drawMode() == CanvasWidget::DrawMode::Line;
      canvas_->setDrawMode(toRect ? CanvasWidget::DrawMode::Rect
                                  : CanvasWidget::DrawMode::Line);
      persistSettings();
      notify_->info(QString("Drawing mode: %1")
                        .arg(toRect ? "Rectangle" : "Line"));
    });

    // ── Instant rectangle (contextMenu.js:425-431): rect mode + begin drawing.
    actDrawRectNow_ = new QAction("Draw Rectangle (instant)", this);
    connect(actDrawRectNow_, &QAction::triggered, this, [this] {
      if (!canvas_->hasImage()) {
        notify_->error("Load an image first");
        return;
      }
      canvas_->setDrawMode(CanvasWidget::DrawMode::Rect);
      canvas_->startDrawingMode();  // continues the selected line if one is set
      notify_->info("Drag to draw a rectangle");
    });

    // ── Style submenu (contextMenu.js:39-57). Marker/thickness spinboxes hosted
    // in QWidgetActions, and an exclusive line-style radio group. All three push
    // canvas DEFAULTS (the context menu, like the toolbar, edits defaults only —
    // selection edits live in the SelectionPanel per the plan's resolution).
    auto styleRow = [this](const QString& label, QSpinBox*& spin, int lo, int hi,
                           QWidgetAction*& act) {
      auto* w = new QWidget(this);
      auto* lay = new QHBoxLayout(w);
      lay->setContentsMargins(14, 4, 14, 4);
      lay->addWidget(new QLabel(label, w));
      spin = new QSpinBox(w);
      spin->setRange(lo, hi);
      lay->addStretch(1);
      lay->addWidget(spin);
      act = new QWidgetAction(this);
      act->setDefaultWidget(w);
    };
    styleRow("Marker Size", markerSpin_, 1, 30, markerSizeAction_);
    styleRow("Line Thickness", thickSpin_, 1, 20, thicknessAction_);
    // Marker / thickness commit on change (contextMenu.js:467-491): defaults +
    // persist + canvas redraw via setDefaults.
    connect(markerSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings_.defaultMarkerSize = v;
              if (markerSize_) {
                QSignalBlocker b(markerSize_);
                markerSize_->setValue(v);  // keep toolbar control in sync
              }
              onLineStyleControlChanged();
            });
    connect(thickSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings_.defaultThickness = v;
              if (lineThickness_) {
                QSignalBlocker b(lineThickness_);
                lineThickness_->setValue(v);
              }
              onLineStyleControlChanged();
            });

    // Line-style radio group (contextMenu.js:51-55, 494-501).
    lineStyleGroup_ = new QActionGroup(this);
    lineStyleGroup_->setExclusive(true);
    auto mkStyle = [this](const QString& text, const QString& value) {
      auto* a = new QAction(text, this);
      a->setCheckable(true);
      a->setData(value);
      lineStyleGroup_->addAction(a);
      connect(a, &QAction::triggered, this,
              [this, value] { applyLineStyle(value); });
      return a;
    };
    actStyleSolid_ = mkStyle("Solid", "solid");
    actStyleDashed_ = mkStyle("Dashed", "dashed");
    actStyleDotted_ = mkStyle("Dotted", "dotted");

    // ── Image Filter submenu (contextMenu.js:59-74, 504-526). Exclusive radio
    // group + a custom-tint picker action shown only when "custom" is active.
    filterGroup_ = new QActionGroup(this);
    filterGroup_->setExclusive(true);
    auto mkFilter = [this](const QString& text, const QString& value) {
      auto* a = new QAction(text, this);
      a->setCheckable(true);
      a->setData(value);
      filterGroup_->addAction(a);
      connect(a, &QAction::triggered, this,
              [this, value] { applyImageFilter(value); });
      return a;
    };
    actFilterNone_ = mkFilter("None", "none");
    actFilterBW_ = mkFilter("Black && White", "bw");
    actFilterSepia_ = mkFilter("Sepia", "sepia");
    actFilterCustom_ = mkFilter("Custom Tint", "custom");
    // Tint color picker (contextMenu.js:518-526): pick the duotone tint, persist,
    // re-apply when the active filter is custom.
    tintColorAction_ = new QAction("Tint Color…", this);
    connect(tintColorAction_, &QAction::triggered, this, [this] {
      const QColor c =
          QColorDialog::getColor(filterColorValue_, this, "Tint color",
                                 QColorDialog::DontUseNativeDialog);
      if (c.isValid()) applyTintColor(c);
    });

    // ── Tooltip row toggles (contextMenu.js:96-107, 546-557). Per-row
    // visibility, backed by the MainWindow booleans (consumed in onHoverDetail).
    // NOTE: these are not yet persisted to Settings (fileStore is frozen for this
    // step) — see the gap note in the structured result.
    auto mkTtRow = [this](const QString& text, bool& backing) {
      auto* a = new QAction(text, this);
      a->setCheckable(true);
      a->setChecked(backing);
      connect(a, &QAction::toggled, this, [this, &backing](bool on) {
        backing = on;
        onHovered(lastHoverX_, lastHoverY_);  // refresh the live tooltip
      });
      return a;
    };
    actTtPage_ = mkTtRow("Page (cm)", tooltipShowPage_);
    actTtScreen_ = mkTtRow("Screen (px)", tooltipShowScreen_);
    actTtCoords_ = mkTtRow("To Edge (cm)", tooltipShowCoords_);

    // ── Units (View ▸ Units): cm | inches, exclusive, persisted in settings_.
    // Switching re-renders every length readout (status bar, tooltip, selection
    // panel) and the custom page spinboxes, which stay backed by cm internally.
    auto* unitGroup = new QActionGroup(this);
    unitGroup->setExclusive(true);
    auto mkUnit = [this, unitGroup](const QString& text, const QString& code) {
      auto* a = new QAction(text, this);
      a->setCheckable(true);
      a->setChecked(settings_.units == code);
      unitGroup->addAction(a);
      connect(a, &QAction::toggled, this, [this, code](bool on) {
        if (on) applyUnits(code);
      });
      return a;
    };
    actUnitCm_ = mkUnit("Centimeters (cm)", "cm");
    actUnitIn_ = mkUnit("Inches (in)", "in");
  }

  void MainWindow::buildMenus() {
    // Keep the menu bar inside the window rather than exported to a native /
    // global app menu (some GNOME setups otherwise render an empty in-window
    // bar), so the multilevel File/Edit/View/Project/Help menus stay visible.
    menuBar()->setNativeMenuBar(false);
    // Mnemonics avoid the Alt+letter combos bound to hotkeys (Alt+F fullscreen,
    // Alt+P points, Alt+L lines, etc.).
    auto* file = menuBar()->addMenu("F&ile");
    file->addAction(actOpen_);
    file->addAction(actOpenAnother_);
    file->addAction(actNewBlank_);
    file->addAction(actCrop_);
    file->addAction(actRotateLeft_);
    file->addAction(actRotateRight_);
    file->addAction(actSaveSession_);
    file->addSeparator();
    file->addAction(actQuit_);

    auto* edit = menuBar()->addMenu("&Edit");
    edit->addAction(actStartDraw_);
    edit->addAction(actStopDraw_);
    edit->addSeparator();
    edit->addAction(actUndo_);
    edit->addAction(actRedo_);
    edit->addSeparator();
    edit->addAction(actNewLine_);
    edit->addAction(actDeleteLast_);
    edit->addAction(actDeleteLine_);
    edit->addAction(actDeletePoint_);
    edit->addAction(actClearAll_);
    edit->addAction(actDeselect_);

    // Data menu (S9): layout JSON file + clipboard, and image save/copy/paste.
    // Mirrors the browser toolbar's Image/Layout button cluster (toolbar.js).
    auto* data = menuBar()->addMenu("&Data");
    data->addAction(actDownloadJson_);
    data->addAction(actUploadJson_);
    data->addSeparator();
    data->addAction(actCopyLayout_);
    data->addAction(actPasteLayout_);
    data->addSeparator();
    data->addAction(actSaveImage_);
    data->addAction(actCopyImage_);
    data->addAction(actPasteImage_);

    auto* view = menuBar()->addMenu("&View");
    view->addAction(actZoomIn_);
    view->addAction(actZoomOut_);
    view->addAction(actFit_);
    view->addSeparator();
    view->addAction(actShowPoints_);
    view->addAction(actShowLines_);
    view->addAction(actPanel_);
    view->addAction(actTooltip_);
    view->addAction(actAllowFormulas_);
    auto* units = view->addMenu("&Units");
    units->addAction(actUnitCm_);
    units->addAction(actUnitIn_);
    view->addSeparator();
    view->addAction(actTheme_);
    view->addAction(actFullscreen_);
    view->addSeparator();
    view->addAction(actIncognito_);
    view->addAction(actSettings_);

    auto* project = menuBar()->addMenu("P&roject");
    project->addAction(actProjects_);
    project->addAction(actConnect_);
    project->addAction(actNewProject_);
    project->addAction(actSaveProject_);
    project->addSeparator();
    // Per-project name colour lives here (not as a toolbar swatch): pick a custom colour or
    // revert to the theme default. Enabled only with an active project (see updateProjectTitle).
    actProjectColor_ = project->addAction("Project &colour…", this, [this] { chooseProjectColor(); });
    actProjectColorClear_ =
        project->addAction("Use theme &default colour", this, [this] { setActiveProjectColor(QString()); });
    actProjectColor_->setEnabled(false);
    actProjectColorClear_->setEnabled(false);
    project->addSeparator();
    project->addAction(actLinks_);

    auto* help = menuBar()->addMenu("&Help");
    help->addAction(actInfo_);
    help->addAction(actShortcuts_);
  }

  // The toolbar is three rows. addToolBar/addToolBarBreak sequencing fixes the
  // visual row order, so the sub-builders MUST run in this order. Each row's
  // widget-creation + wiring stays grouped in one method (the Style row's
  // connects reference widgets it creates).
  void MainWindow::buildToolbar() {
    buildMainToolbar();
    buildPageFormulaToolbar();
    buildStyleToolbar();
  }

  void MainWindow::buildMainToolbar() {
    // Two rows so nothing is pushed into QToolBar's "»" overflow (which is what
    // hid the formula inputs / custom-page inputs at normal window widths). Row 1:
    // file + drawing + history + zoom. Row 2: page size (+custom) + formulas.
    auto* tb = addToolBar("Main");
    tb->setMovable(false);
    // Icon-only with the shared line-art glyphs (styleActionIcons assigns them) +
    // the rich tooltips from mk(): compact, browser-faithful chrome that stays
    // narrow enough to avoid the "»" overflow even with the full action set.
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->setIconSize(QSize(18, 18));

    tb->addAction(actOpen_);
    tb->addAction(actOpenAnother_);
    tb->addAction(actNewBlank_);
    tb->addAction(actLinks_);
    // Connect/Servers affordance on the top bar (mirrors the browser's #connect-btn
    // icon button) — not just the Project-menu entry, so it is one click away.
    tb->addAction(actConnect_);
    // Incognito as a checkable toolbar icon (mirrors the browser's #incognito-toggle):
    // bound to the SAME actIncognito_ as the View-menu entry, so toggle state, the
    // active highlight (QAction checked), and the disabled-when-an-image-is-loaded
    // gating (refreshActions) stay in lockstep across both surfaces.
    tb->addAction(actIncognito_);
    tb->addAction(actCrop_);
    tb->addAction(actRotateLeft_);
    tb->addAction(actRotateRight_);
    tb->addSeparator();
    tb->addAction(actStartDraw_);
    tb->addAction(actStopDraw_);
    tb->addAction(actNewLine_);
    tb->addSeparator();
    tb->addAction(actUndo_);
    tb->addAction(actRedo_);
    tb->addSeparator();
    tb->addWidget(new QLabel("  Zoom: ", this));
    tb->addWidget(zoom_);

    // ── Project name field + inline-rename ✓/✗ (mirrors the browser topbar). The
    // field shows the active project's name and renames it inline, validated live:
    // ✓ is enabled only for a changed, valid (non-empty, ≤80, unique) name, with the
    // reason on its tooltip when disabled. Enter = ✓, Escape / click-away = ✗. ──
    tb->addSeparator();
    tb->addWidget(new QLabel("  Project: ", this));
    projectName_ = new QLineEdit(this);
    projectName_->setPlaceholderText("No project");
    projectName_->setToolTip("Project name — double-click (or ✎) to rename");
    projectName_->setMinimumWidth(150);
    projectName_->setMaximumWidth(260);
    projectName_->setEnabled(false);
    projectName_->setReadOnly(true);  // browser-like: read-only until edit mode (✎ / double-click)
    tb->addWidget(projectName_);
    // Browser-style affordances beside the name: a ✎ rename pencil (focuses + selects the field)
    // and a 🎨 colour icon (flat — NOT a filled swatch — opening choose / theme-default). Both
    // are themed line-art glyphs (styleActionIcons) and enable only with an active project.
    projectNameEdit_ = new QToolButton(this);
    projectNameEdit_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    projectNameEdit_->setAutoRaise(true);
    projectNameEdit_->setToolTip("Rename project");
    projectNameEdit_->setEnabled(false);
    projectNameEditAction_ = tb->addWidget(projectNameEdit_);
    connect(projectNameEdit_, &QToolButton::clicked, this, [this] { enterNameEdit(); });
    projectColorBtn_ = new QToolButton(this);
    projectColorBtn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    projectColorBtn_->setAutoRaise(true);
    projectColorBtn_->setToolTip("Project name colour — right-click to reset to default");
    projectColorBtn_->setEnabled(false);
    projectColorBtnAction_ = tb->addWidget(projectColorBtn_);
    // Click opens a small menu (browser parity): "Choose colour…" + "Use theme default colour".
    // The menu runs its own loop and fully closes before we open the picker (deferred), so no stray
    // grab dismisses the dialog. Right-click still resets straight to the theme default.
    connect(projectColorBtn_, &QToolButton::clicked, this, [this] { showProjectColorMenu(); });
    projectColorBtn_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(projectColorBtn_, &QToolButton::customContextMenuRequested, this,
            [this](const QPoint&) { setActiveProjectColor(QString()); });
    // Inline-rename confirm/cancel: line-art check / x glyphs (themed in
    // styleActionIcons) instead of the bare ✓/✗ text, matching the browser's
    // icon buttons. Icon-only with a tooltip.
    projectNameAccept_ = new QToolButton(this);
    projectNameAccept_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    projectNameAccept_->setToolTip("Rename (Enter)");
    projectNameAccept_->setVisible(false);
    projectNameAcceptAction_ = tb->addWidget(projectNameAccept_);
    projectNameAcceptAction_->setVisible(false);
    projectNameCancel_ = new QToolButton(this);
    projectNameCancel_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    projectNameCancel_->setToolTip("Cancel (Esc)");
    projectNameCancel_->setVisible(false);
    projectNameCancelAction_ = tb->addWidget(projectNameCancel_);
    projectNameCancelAction_->setVisible(false);
    // The per-project name colour control is NOT a toolbar swatch — it lives in the Project
    // menubar menu (actProjectColor_ / actProjectColorClear_). projectColorBtn_ stays null; the
    // active project's colour is still visible because the name field itself is painted in it.
    // textEdited fires only on USER edits (not programmatic setText), so updating the
    // field from updateProjectTitle() never re-triggers validation.
    connect(projectName_, &QLineEdit::textEdited, this,
            [this](const QString&) { refreshProjectNameButtons(); });
    connect(projectName_, &QLineEdit::returnPressed, this, [this] {
      if (nameEditing_) commitProjectName();  // commit (no-op if unchanged) + leave edit mode
    });
    connect(projectNameAccept_, &QToolButton::clicked, this, [this] { commitProjectName(); });
    connect(projectNameCancel_, &QToolButton::clicked, this, [this] { cancelProjectName(); });
    // Escape cancels the edit; clicking away (focus-out) reverts any uncommitted text — both via
    // the event filter below, so the user can always leave the field (Enter still commits).
    projectName_->installEventFilter(this);
  }

  void MainWindow::buildPageFormulaToolbar() {
    // ── second row ──
    addToolBarBreak();
    auto* tb2 = addToolBar("Page & Formula");
    tb2->setMovable(false);
    tb2->setToolButtonStyle(Qt::ToolButtonTextOnly);

    tb2->addWidget(new QLabel(" Page: ", this));
    tb2->addWidget(pageSize_);

    // Units switch on the toolbar (mirrors View ▸ Units, kept in sync). data
    // carries the canonical code; both surfaces route through applyUnits().
    tb2->addWidget(new QLabel(" Units: ", this));
    unitCombo_ = new QComboBox(this);
    unitCombo_->addItem("cm", "cm");
    unitCombo_->addItem("in", "in");
    unitCombo_->setToolTip("Display units (cm / inches)");
    connect(unitCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applyUnits(unitCombo_->currentData().toString()); });
    tb2->addWidget(unitCombo_);

    // Inline custom W x H inputs (S10), shown only for the "custom" page size.
    customGroup_ = new QWidget(this);
    {
      auto* cl = new QHBoxLayout(customGroup_);
      cl->setContentsMargins(4, 0, 0, 0);
      cl->setSpacing(2);
      customW_ = new QDoubleSpinBox(customGroup_);
      customW_->setRange(0.1, 500.0);  // browser LIMITS custom page bounds
      customW_->setSingleStep(0.1);
      customW_->setDecimals(1);
      customW_->setValue(21.0);
      customW_->setToolTip("Custom page width in the selected units");
      // Width-tightening (S8 req 7): keep the custom-page spinboxes compact
      // (browser style width:96px, toolbar.js:110/112).
      customW_->setMaximumWidth(96);
      customW_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
      customH_ = new QDoubleSpinBox(customGroup_);
      customH_->setRange(0.1, 500.0);
      customH_->setSingleStep(0.1);
      customH_->setDecimals(1);
      customH_->setValue(29.7);
      customH_->setToolTip("Custom page height in the selected units");
      customH_->setMaximumWidth(96);
      customH_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
      cl->addWidget(customW_);
      cl->addWidget(new QLabel("×", customGroup_));
      cl->addWidget(customH_);
      customUnitLabel_ = new QLabel("cm", customGroup_);
      cl->addWidget(customUnitLabel_);
    }
    // Toggle the QWidgetAction (not the widget) so the toolbar re-lays-out and
    // actually makes room for the inputs — setVisible() on the widget alone
    // leaves a zero-width slot, so the spinboxes never appeared.
    customGroupAct_ = tb2->addWidget(customGroup_);
    customGroupAct_->setVisible(false);
    tb2->addSeparator();

    // Inline formula controls (S11): an enable checkbox + fx/fy inputs + error.
    allowFormulas_ = new QCheckBox("f(x,y)", this);
    allowFormulas_->setToolTip(
        "Enable x/y coordinate transform formulas applied to the points table");
    tb2->addWidget(allowFormulas_);
    formulaGroup_ = new QWidget(this);
    {
      auto* fl = new QHBoxLayout(formulaGroup_);
      fl->setContentsMargins(4, 0, 0, 0);
      fl->setSpacing(2);
      formulaX_ = new QLineEdit(formulaGroup_);
      formulaX_->setPlaceholderText("x(x)=");
      formulaX_->setToolTip("Transform formula for x — e.g. x*2 + 1 (empty = identity)");
      // Width-tightening (S8 req 7): compact f(x,y) inputs (browser width:90px,
      // toolbar.js:119/120) with a Fixed policy so they don't stretch.
      formulaX_->setMaximumWidth(90);
      formulaX_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
      formulaY_ = new QLineEdit(formulaGroup_);
      formulaY_->setPlaceholderText("y(y)=");
      formulaY_->setToolTip("Transform formula for y — e.g. y/2 (empty = identity)");
      formulaY_->setMaximumWidth(90);
      formulaY_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
      formulaError_ = new QLabel("⚠ invalid", formulaGroup_);
      formulaError_->setStyleSheet("color:#d9534f;");
      formulaError_->setVisible(false);
      fl->addWidget(new QLabel("x=", formulaGroup_));
      fl->addWidget(formulaX_);
      fl->addWidget(new QLabel("y=", formulaGroup_));
      fl->addWidget(formulaY_);
      fl->addWidget(formulaError_);
    }
    // Toggle the QWidgetAction (not the widget) so the toolbar re-lays-out and
    // makes room for the inputs — same fix as the custom-page group.
    formulaGroupAct_ = tb2->addWidget(formulaGroup_);
    formulaGroupAct_->setVisible(false);
    // Theme / Incognito / Settings / Projects / Info live in the menu bar only —
    // keeping them off the toolbar prevents overflow from hiding the inline
    // formula inputs (and mirrors the browser's leaner top bar).
  }

  void MainWindow::buildStyleToolbar() {
    // ── third row: Style (filter + line defaults + draw-mode) ──
    // Mirrors the browser toolbar's Image (filter) + Line Style + Draw sections
    // (toolbar.js ~24-63). The toolbar drives canvas DEFAULTS + the image filter;
    // selected-line editing lives in the SelectionPanel (Step 10).
    addToolBarBreak();
    auto* tb3 = addToolBar("Style");
    tb3->setMovable(false);
    tb3->setToolButtonStyle(Qt::ToolButtonTextOnly);

    // Image filter combo (toolbar.js:24-29). data carries the canonical value.
    tb3->addWidget(new QLabel(" Filter: ", this));
    imageFilter_ = new QComboBox(this);
    imageFilter_->addItem("No Filter", "none");
    imageFilter_->addItem("B&W", "bw");
    imageFilter_->addItem("Sepia", "sepia");
    imageFilter_->addItem("Tint", "custom");
    imageFilter_->setToolTip("Image filter: none, black & white, sepia, or tint");
    tb3->addWidget(imageFilter_);

    // Tint swatch (toolbar.js:30 #filterColor), hidden unless the "custom" filter
    // is selected. The QWidgetAction handle is toggled so the toolbar re-lays-out.
    filterColorBtn_ = new QToolButton(this);
    filterColorBtn_->setToolTip("Tint color");
    updateColorSwatch(filterColorBtn_, filterColorValue_);
    filterColorAct_ = tb3->addWidget(filterColorBtn_);
    filterColorAct_->setVisible(false);
    tb3->addSeparator();

    // Default line color swatch (toolbar.js:40 #lineColor).
    tb3->addWidget(new QLabel(" Line: ", this));
    lineColorBtn_ = new QToolButton(this);
    lineColorBtn_->setToolTip("Line color");
    updateColorSwatch(lineColorBtn_, lineColorValue_);
    tb3->addWidget(lineColorBtn_);

    // Thickness / marker spinboxes (toolbar.js:41-42, min/max mirrored). Fixed
    // narrow width so they don't sprawl (req: setMaximumWidth(56) + Fixed policy).
    // Each gets a visible caption so the bare numbers aren't cryptic.
    tb3->addWidget(new QLabel(" Thickness: ", this));
    lineThickness_ = new QSpinBox(this);
    lineThickness_->setRange(1, 20);
    lineThickness_->setValue(2);
    lineThickness_->setToolTip("Line thickness");
    lineThickness_->setMaximumWidth(56);
    lineThickness_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    tb3->addWidget(lineThickness_);
    tb3->addWidget(new QLabel(" Marker: ", this));
    markerSize_ = new QSpinBox(this);
    markerSize_->setRange(1, 30);
    markerSize_->setValue(4);
    markerSize_->setToolTip("Marker size");
    markerSize_->setMaximumWidth(56);
    markerSize_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    tb3->addWidget(markerSize_);

    // Line-style combo (toolbar.js:43-47). data carries the canonical value.
    tb3->addWidget(new QLabel(" Style: ", this));
    lineStyle_ = new QComboBox(this);
    lineStyle_->addItem("Solid", "solid");
    lineStyle_->addItem("Dashed", "dashed");
    lineStyle_->addItem("Dotted", "dotted");
    lineStyle_->setToolTip("Line style");
    tb3->addWidget(lineStyle_);
    tb3->addSeparator();

    // Draw-mode toggle (toolbar.js:59 #drawModeToggle). Flips line<->rect.
    drawModeBtn_ = new QToolButton(this);
    // Icon + label (the glyph is themed in styleActionIcons / the handler below);
    // the box-drawing prefix is replaced by the shared pencil / filled-rect icon.
    drawModeBtn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    drawModeBtn_->setText("Line");
    drawModeBtn_->setToolTip(
        "Drawing mode: Line (click to switch to Rectangle)");
    tb3->addWidget(drawModeBtn_);

    // ── wiring ──
    // Draw-mode toggle (drawingApp.js:298-301): flip the canvas mode. The
    // drawModeChanged signal handler below keeps the button text/title in sync,
    // so this need only toggle the canvas (which echoes back).
    connect(drawModeBtn_, &QToolButton::clicked, this, [this] {
      const auto next = canvas_->drawMode() == CanvasWidget::DrawMode::Rect
                            ? CanvasWidget::DrawMode::Line
                            : CanvasWidget::DrawMode::Rect;
      canvas_->setDrawMode(next);
      persistSettings();
    });
    // Echo the canvas draw mode onto the toggle button (drawingApp.js
    // syncDrawModeUI ~1125): label + tooltip per mode.
    connect(canvas_, &CanvasWidget::drawModeChanged, this,
            [this](CanvasWidget::DrawMode mode) {
              const bool rect = mode == CanvasWidget::DrawMode::Rect;
              drawModeBtn_->setText(rect ? "Rect" : "Line");
              drawModeBtn_->setIcon(
                  themedIcon(rect ? "rect-filled" : "pencil", iconColor_, 16));
              drawModeBtn_->setToolTip(
                  rect ? "Drawing mode: Rectangle (click to switch to Line)"
                       : "Drawing mode: Line (click to switch to Rectangle)");
            });

    // Default line color (drawingApp.js:155): pick a color, store as the default
    // and push to the canvas.
    connect(lineColorBtn_, &QToolButton::clicked, this, [this] {
      const QColor c = QColorDialog::getColor(lineColorValue_, this, "Line color",
                                              QColorDialog::DontUseNativeDialog);
      if (!c.isValid()) return;
      lineColorValue_ = c;
      updateColorSwatch(lineColorBtn_, c);
      settings_.defaultColor = c.name(QColor::HexRgb);
      onLineStyleControlChanged();
    });
    // Thickness / marker / style → defaults (drawingApp.js:156-178).
    connect(lineThickness_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings_.defaultThickness = v;
              if (thickSpin_) {  // keep the context-menu spinbox in sync (two-way)
                QSignalBlocker b(thickSpin_);
                thickSpin_->setValue(v);
              }
              onLineStyleControlChanged();
            });
    connect(markerSize_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings_.defaultMarkerSize = v;
              if (markerSpin_) {  // keep the context-menu spinbox in sync (two-way)
                QSignalBlocker b(markerSpin_);
                markerSpin_->setValue(v);
              }
              onLineStyleControlChanged();
            });
    connect(lineStyle_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
              applyLineStyle(lineStyle_->currentData().toString());
            });

    // Image filter combo (drawingApp.js:228-238): set mode, toggle tint swatch
    // visibility, apply to the canvas + persist (shared with the context menu).
    connect(imageFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
              applyImageFilter(imageFilter_->currentData().toString());
            });
    // Tint color (drawingApp.js:240-249): pick the custom duotone tint.
    connect(filterColorBtn_, &QToolButton::clicked, this, [this] {
      const QColor c = QColorDialog::getColor(filterColorValue_, this, "Tint color",
                                              QColorDialog::DontUseNativeDialog);
      if (c.isValid()) applyTintColor(c);
    });
  }

  // Push the current default visuals to the canvas and persist (S8). Mirrors the
  // browser change handlers that update this.color/thickness/markerSize/style then
  // storage.save() (drawingApp.js:155-178). Defaults ONLY — never the selection.
  void MainWindow::onLineStyleControlChanged() {
    canvas_->setDefaults(settings_.defaultColor, settings_.defaultThickness,
                         settings_.defaultMarkerSize, settings_.defaultStyle);
    persistSettings();
  }

  // ── Shared apply paths for controls duplicated in the toolbar AND context menu.
  // Both UIs route through these so they never drift (the toolbar combo and the
  // context-menu radio group stay mutually in sync) and the apply/persist logic
  // lives once. Setting an exclusive QAction's checked state emits toggled(), not
  // triggered(), so re-checking the group action here never re-enters this path.
  void MainWindow::applyImageFilter(const QString& mode) {
    settings_.imageFilter = mode;
    if (imageFilter_) {  // sync toolbar combo by canonical data value
      const int idx = imageFilter_->findData(mode);
      if (idx >= 0) {
        QSignalBlocker b(imageFilter_);
        imageFilter_->setCurrentIndex(idx);
      }
    }
    if (filterGroup_) {  // sync context-menu radio group
      for (QAction* a : filterGroup_->actions())
        if (a->data().toString() == mode) { a->setChecked(true); break; }
    }
    if (filterColorAct_) filterColorAct_->setVisible(mode == "custom");
    canvas_->setImageFilter(mode, filterColorValue_);
    persistSettings();
    if (!remoteReloading_) filterDirty_ = true;   // user changed the filter
    scheduleRemotePush();   // live co-edit: a filter change isn't a canvas changed()
  }

  void MainWindow::applyTintColor(const QColor& color) {
    filterColorValue_ = color;
    settings_.filterColor = color.name(QColor::HexRgb);
    if (filterColorBtn_) updateColorSwatch(filterColorBtn_, color);
    canvas_->setImageFilter(settings_.imageFilter, filterColorValue_);
    persistSettings();
    if (!remoteReloading_) filterDirty_ = true;   // user changed the tint
    scheduleRemotePush();   // live co-edit: push tint changes to peers
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
    setColorSwatch(btn, color);  // QToolButton derives from QAbstractButton
  }

  // Load a local file as a fresh image, page-sized to the current page setting, clearing
  // any source/resource provenance. Notifies + refreshes actions. Returns whether it loaded.
  bool MainWindow::loadLocalImageReset(const QString& path) {
    const core::PageSize page = naturalPageCm(pageSize_->currentText(),
                                              settings_.customPageWidth,
                                              settings_.customPageHeight);
    canvas_->setPageCm(page.width, page.height);
    if (!canvas_->loadImage(path)) {
      notify_->error("Failed to load image");
      return false;
    }
    currentSource_.clear();  // a local file has no source/resource provenance
    currentResource_.clear();
    refreshActions();
    notify_->success("Image loaded");
    return true;
  }

  void MainWindow::openImage() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open image", QString(), "Images (*.png *.jpg *.jpeg *.bmp *.gif)");
    if (path.isEmpty()) return;
    loadLocalImageReset(path);
  }

  // "Open another image" (mirrors browser openImageModal.js): pick a file + an
  // incognito flag, then either replace this editor or launch the image in a new
  // window. The dialog returns the chosen outcome; we dispatch accordingly.
  void MainWindow::openAnotherImage() {
    OpenImageDialog dlg(this, canReplaceActive());
    if (dlg.exec() != QDialog::Accepted) return;
    const QString path = dlg.path();
    if (path.isEmpty()) return;
    const OpenImageDialog::Outcome outcome = dlg.outcome();
    if (outcome == OpenImageDialog::Outcome::NewWindow) {
      openImageInNewWindow(path, dlg.incognito());
    } else if (outcome == OpenImageDialog::Outcome::Replace) {
      replaceProjectImage(path, dlg.rename(), dlg.keepAnnotations());
    } else {
      openImageHere(path, dlg.incognito());
    }
  }

  // True when the current editor holds a saved/linked project whose image can be swapped in
  // place (not a blank or incognito session — there's nothing to keep the same).
  bool MainWindow::canReplaceActive() const {
    return canvas_->hasImage() && !incognito_
        && (!activeProjectId_.isEmpty() || !remoteAddress_.isEmpty());
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
      if (!remoteAddress_.isEmpty()) {
        remoteName_ = newName;
      } else if (Project* pr = findProject(activeProjectId_.toStdString())) {
        pr->meta.name = newName.toStdString();
      }
      updateProjectTitle();
    }
    // Server-linked: re-upload the new original (saveToServer only pushes the result), then
    // saveToActiveProject pushes the layout + rendered result.
    if (!remoteAddress_.isEmpty()) replaceServerOriginal();
    saveToActiveProject();
  }

  // Re-upload the linked server project's `original` with the current canvas image, refreshing
  // the version guard. No-op when not server-linked or sync is off (matches edit-in-memory).
  void MainWindow::replaceServerOriginal() {
    if (remoteAddress_.isEmpty() || !settings_.syncToServer) return;
    stencil::net::ServerClient* c = connections_ ? connections_->find(remoteAddress_) : nullptr;
    if (!c || !canvas_->hasImage()) return;
    const int w = canvas_->imageWidth();
    const int h = canvas_->imageHeight();
    if (c->uploadFile(remoteId_, "original", pngBytes(canvas_->image()), "png", w, h)) {
      stencil::net::ServerProject meta;
      QJsonObject lay;
      if (c->getProject(remoteId_, meta, lay)) remoteVersion_ = meta.version;
    }
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
    createServerProject(serverUrl, name);   // create + upload original + link the session
    if (remoteId_.isEmpty()) return;          // creation failed (already notified)
    // Push the annotated layout + result now, regardless of the sync toggle (explicit publish).
    const bool savedSync = settings_.syncToServer;
    settings_.syncToServer = true;
    saveToServer();
    settings_.syncToServer = savedSync;
    startRemotePoll();   // live co-edit: watch for peers changing this project
    refreshActions();
    updateProjectTitle();
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
    loadLocalImageReset(path);
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
  void MainWindow::newBlankImage() {
    const auto px = core::defaultBlankSizePx(currentPageDimensions());
    BlankImageDialog dlg(px.width, px.height, this);
    if (dlg.exec() != QDialog::Accepted) return;
    if (canvas_->hasImage() &&
        QMessageBox::question(this, "Replace image",
                              "Replace the current image with a new blank image?")
            != QMessageBox::Yes) {
      notify_->info("Blank image canceled");
      return;
    }
    QImage img(dlg.widthPx(), dlg.heightPx(), QImage::Format_RGB32);
    img.fill(dlg.color());
    {
      const core::PageSize page = naturalPageCm(pageSize_->currentText(),
                                                settings_.customPageWidth,
                                                settings_.customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    canvas_->loadFromImage(img);
    currentSource_.clear();  // a generated blank image has no provenance
    currentResource_.clear();
    refreshActions();
    notify_->success(QString("Blank %1×%2 image created")
                         .arg(dlg.widthPx())
                         .arg(dlg.heightPx()));
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
        pageSize_->currentText(), settings_.customPageWidth, settings_.customPageHeight);
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
            "markers. Continue?") != QMessageBox::Yes) {
      notify_->info("Crop canceled");
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

  // Page dimensions for the current selection, honoring custom W x H (S10).
  core::PageSize MainWindow::currentPageDimensions() const {
    return core::pageDimensions(pageSize_->currentText().toStdString(),
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
    p.x = formula_.apply(settings_.formulaX.toStdString(), 'x', raw.x,
                         settings_.allowFormulas);
    p.y = formula_.apply(settings_.formulaY.toStdString(), 'y', raw.y,
                         settings_.allowFormulas);
    return p;
  }

  // Active display unit (cm by default; inches scales cm by 1/2.54). Shared with
  // the hover tooltip via core::buildTooltipRows.
  core::UnitFormat MainWindow::unitFormat() const {
    if (settings_.units == "in") return {1.0 / 2.54, "in"};
    return {1.0, "cm"};
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
    applyUnitToPageInputs();
    onHovered(lastHoverX_, lastHoverY_);  // status bar + live tooltip
    onSelectionChanged();                 // selection panel rows
  }

  // Reuse the core page metrics exactly as the browser's pixelToPageCoords does,
  // so the page readout matches between the two front-ends. Status mirrors the
  // browser status bar: Pixel / Page / To edge, in brackets, in the active unit.
  void MainWindow::onHovered(double imageX, double imageY) {
    if (!canvas_->hasImage()) return;
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
    const bool custom = pageSize_->currentText() == "custom";
    if (customGroupAct_) customGroupAct_->setVisible(custom);
    settings_.pageSize = pageSize_->currentText();
    // Keep the canvas's default-crop aspect in sync with the selected page.
    {
      const core::PageSize page = naturalPageCm(pageSize_->currentText(),
                                                settings_.customPageWidth,
                                                settings_.customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    persistSettings();
    onHovered(lastHoverX_, lastHoverY_);
    onSelectionChanged();  // refresh panel cm for the new page size (GAP-2)
    scheduleRemotePush();  // page format rides the layout — push to peers
  }

  // Validate fx/fy at input time and apply them (S11; drawingApp.js
  // validateAndApplyFormulas ~270). Invalid expressions show an inline error and
  // are not applied; valid ones persist and refresh the readout.
  void MainWindow::validateAndApplyFormulas() {
    const QString fx = formulaX_->text().trimmed();
    const QString fy = formulaY_->text().trimmed();
    const bool okX = formula_.validate(fx.toStdString(), 'x');
    const bool okY = formula_.validate(fy.toStdString(), 'y');
    formulaError_->setVisible(!okX || !okY);
    if (okX && okY) {
      settings_.formulaX = fx;
      settings_.formulaY = fy;
      persistSettings();
      onHovered(lastHoverX_, lastHoverY_);
      onSelectionChanged();  // refresh panel cm with the new formulas (GAP-2)
      scheduleRemotePush();  // formulas ride the layout — push to peers
    }
  }

  void MainWindow::refreshActions() {
    actUndo_->setEnabled(canvas_->canUndo());
    actRedo_->setEnabled(canvas_->canRedo());
    actSaveProject_->setEnabled(!activeProjectId_.isEmpty());
    // Start only when an image is loaded and not already drawing; Stop only while
    // drawing (mirrors the browser HK_HANDLERS startDraw/stopDraw guards).
    const bool drawing = canvas_->isDrawing();
    actStartDraw_->setEnabled(canvas_->hasImage() && !drawing);
    actStopDraw_->setEnabled(drawing);
    // Incognito can only be toggled before an image exists (S6).
    actIncognito_->setEnabled(!canvas_->hasImage());
    // Data actions (S9): layout export/copy need lines; importing a layout and
    // every image action need an image first (mirrors the browser guards). Paste
    // stays enabled so the Ctrl+V dispatch can still notify "Load an image first".
    const bool hasImg = canvas_->hasImage();
    const bool hasLines = !canvas_->allLines().empty();
    actDownloadJson_->setEnabled(hasLines);
    actCopyLayout_->setEnabled(hasLines);
    actUploadJson_->setEnabled(hasImg);
    actPasteLayout_->setEnabled(hasImg);
    actSaveImage_->setEnabled(hasImg);
    actCopyImage_->setEnabled(hasImg);
    updateProjectTitle();   // keep the window title + toolbar name field in sync
  }

  void MainWindow::onCanvasChanged() {
    refreshActions();
    onSelectionChanged();
    scheduleAutosave();
    scheduleRemotePush();   // live co-edit: push the edit to the server for peers
  }

  void MainWindow::scheduleRemotePush() {
    // Sync off → a fetched project is edit-in-memory only: never auto-push to peers.
    if (incognito_ || remoteReloading_ || remoteAddress_.isEmpty() || !settings_.syncToServer) return;
    // Trailing debounce (coalesce a burst of edits into one save) capped by a max-wait, so
    // continuous editing still flushes to peers every ~1.5s instead of starving until a pause.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (remotePushBurstStart_ == 0) remotePushBurstStart_ = now;
    const int wait = std::clamp<int>(1500 - static_cast<int>(now - remotePushBurstStart_), 0, 350);
    remotePushTimer_->start(wait);
  }

  void MainWindow::startRemotePoll() {
    if (remotePollTimer_ && !remoteAddress_.isEmpty()) remotePollTimer_->start();
    // Subscribe the live push feed so peer edits arrive in tens of ms; the poll above is
    // now just a backstop (https servers / a dropped socket).
    ensureLiveFeed();
  }

  void MainWindow::stopRemotePoll() {
    if (remotePollTimer_) remotePollTimer_->stop();
    if (remoteReloadTimer_) remoteReloadTimer_->stop();
    if (liveFeed_) liveFeed_->unsubscribe();
  }

  // Lazily build the push feed and (re)point it at the active server, authenticating with
  // that connection's token. A no-op when the session isn't server-linked or the server
  // isn't connected. subscribe() is idempotent for the same origin (token refresh only).
  void MainWindow::ensureLiveFeed() {
    if (remoteAddress_.isEmpty()) return;
    stencil::net::ServerClient* c = connections_ ? connections_->find(remoteAddress_) : nullptr;
    if (!c) return;
    if (!liveFeed_) {
      liveFeed_ = new stencil::net::LiveFeed(this);
      connect(liveFeed_, &stencil::net::LiveFeed::projectUpdated,
              this, &MainWindow::onRemoteProjectEvent);
    }
    liveFeed_->subscribe(remoteAddress_, c->token());
  }

  // A live-feed push frame arrived. Reload (debounced) when it's a genuine peer change to
  // the project we're editing — newer version, not our own echo, not mid-push. Mirrors the
  // browser's onServerProjectEvent + shouldReloadFromEvent guards; the actual reload runs
  // from remoteReloadTimer_ so it lands off this slot and coalesces a burst.
  void MainWindow::onRemoteProjectEvent(const QString& id, qint64 version, bool deleted) {
    if (remoteAddress_.isEmpty() || remoteId_.isEmpty()) return;
    if (id != remoteId_ || deleted) return;
    if (!settings_.syncToServer) return;
    if (version <= remoteVersion_) return;  // our own save echo, or stale
    // A reload is mid-flight (its nested loop is pumping this slot): queue one more pass
    // rather than dropping the change — the reload's tail re-arms from remoteReloadPending_.
    if (remoteReloading_) { remoteReloadPending_ = true; return; }
    // Coalesce a burst of peer events into a single debounced reload. The timer slot
    // re-checks the push guards at fire time (state may change within the window).
    if (remoteReloadTimer_) remoteReloadTimer_->start(40);
  }

  // One poll tick: if a peer bumped the linked project's version, reload the canvas.
  // Skipped while a local edit is pending/in-flight so we never clobber the user's work
  // or reload our own change.
  void MainWindow::pollRemoteForUpdate() {
    if (remoteAddress_.isEmpty() || remoteId_.isEmpty()) return;
    if (!settings_.syncToServer) return;  // sync off — don't pull peer changes over local edits
    if (remotePushing_ || (remotePushTimer_ && remotePushTimer_->isActive())) return;
    stencil::net::ServerClient* c =
        connections_ ? connections_->find(remoteAddress_) : nullptr;
    if (!c) return;
    stencil::net::ServerProject meta;
    QJsonObject layout;
    if (!c->getProject(remoteId_, meta, layout)) return;
    if (meta.version > remoteVersion_)
      openServerProject(remoteAddress_, remoteId_, /*silent=*/true);
  }

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
  }

  // Canvas right-click menu — mirrors the grouping of browser/js/ui/contextMenu.js
  // (drawing · view/zoom · toggles · transform), reusing the shared QActions so
  // labels, checkmarks and enabled-state stay in sync with the toolbar/menubar.
  void MainWindow::showContextMenu(const QPoint& globalPos) {
    syncContextActions();

    // ── Build the menu tree. Order mirrors contextMenu.js inner() (~5-108):
    // Image/Layout · Fullscreen · Fit · — · Draw · DrawMode · DrawRect · — ·
    // Show Points/Lines · Clear · — · Style · Filter · Transformation · Tooltip.
    QMenu menu(this);

    // Image / Layout submenu (contextMenu.js:7-22).
    QMenu* layout = menu.addMenu("Image / Layout");
    layout->addAction(actCopyImage_);
    layout->addAction(actPasteImage_);
    layout->addAction(actSaveImage_);  // "Download Image"
    layout->addSeparator();
    layout->addAction(actCopyLayout_);
    layout->addAction(actPasteLayout_);
    layout->addAction(actDownloadJson_);  // "Download Layout"
    layout->addAction(actUploadJson_);    // "Upload Layout"

    // Flat Fullscreen + Fit (contextMenu.js:23-26).
    menu.addAction(actFullscreen_);
    menu.addAction(actFit_);
    menu.addSeparator();

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
    QMenu* style = menu.addMenu("Style");
    style->addAction(markerSizeAction_);
    style->addAction(thicknessAction_);
    style->addSeparator();
    style->addAction(actStyleSolid_);
    style->addAction(actStyleDashed_);
    style->addAction(actStyleDotted_);

    // Image Filter submenu (contextMenu.js:59-74).
    QMenu* filter = menu.addMenu("Image Filter");
    filter->addAction(actFilterNone_);
    filter->addAction(actFilterBW_);
    filter->addAction(actFilterSepia_);
    filter->addAction(actFilterCustom_);
    filter->addSeparator();
    filter->addAction(tintColorAction_);

    // Transformation submenu (contextMenu.js:76-95): reuse the formulas toggle.
    QMenu* transform = menu.addMenu("Transformation");
    transform->addAction(actAllowFormulas_);

    // Tooltip submenu (contextMenu.js:96-107): enable toggle + the 3 row toggles.
    QMenu* tt = menu.addMenu("Tooltip");
    tt->addAction(actTooltip_);
    tt->addSeparator();
    tt->addAction(actTtPage_);
    tt->addAction(actTtScreen_);
    tt->addAction(actTtCoords_);

    menu.addSeparator();
    menu.addAction(actDeselect_);

    menu.exec(globalPos);
  }

  // Live-sync the persistent submenu state before exec (mirrors the browser
  // syncState() in contextMenu.js:239-297, which runs on open + on a timer).
  void MainWindow::syncContextActions() {
    const bool hasImg = canvas_->hasImage();
    const bool hasLines = !canvas_->allLines().empty();

    // Image / Layout enable-state (contextMenu.js:254-264).
    actCopyImage_->setEnabled(hasImg);
    actSaveImage_->setEnabled(hasImg);
    actPasteImage_->setEnabled(true);  // dispatch notifies "Load an image first"
    actCopyLayout_->setEnabled(hasLines);
    actDownloadJson_->setEnabled(hasLines);
    actPasteLayout_->setEnabled(hasImg);
    actUploadJson_->setEnabled(hasImg);

    // Draw-mode bridge label (contextMenu.js:249-252).
    const bool isRect = canvas_->drawMode() == CanvasWidget::DrawMode::Rect;
    actDrawModeToggle_->setText(isRect ? "Switch to Line Drawing"
                                       : "Switch to Rectangle Drawing");

    // Style submenu values (contextMenu.js:274-276). Block so seeding the
    // spinboxes/radios doesn't re-fire change handlers.
    {
      QSignalBlocker bm(markerSpin_), bt(thickSpin_);
      markerSpin_->setValue(settings_.defaultMarkerSize);
      thickSpin_->setValue(settings_.defaultThickness);
    }
    for (QAction* a : lineStyleGroup_->actions())
      a->setChecked(a->data().toString() == settings_.defaultStyle);

    // Image-filter submenu (contextMenu.js:278-282): check the active filter and
    // show the tint action only for custom.
    for (QAction* a : filterGroup_->actions())
      a->setChecked(a->data().toString() == settings_.imageFilter);
    tintColorAction_->setVisible(settings_.imageFilter == "custom");

    // Tooltip rows (contextMenu.js:289-293).
    {
      QSignalBlocker bp(actTtPage_), bs(actTtScreen_), bc(actTtCoords_);
      actTtPage_->setChecked(tooltipShowPage_);
      actTtScreen_->setChecked(tooltipShowScreen_);
      actTtCoords_->setChecked(tooltipShowCoords_);
    }
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
    const double scale = canvas_->scale();
    const auto dims = currentPageDimensions();

    auto rowsForPoint = [&](double px, double py) {
      const auto page = pageCoords(px, py);
      // Per-row visibility from the context-menu Tooltip submenu (S11; mirrors
      // contextMenu.js tooltipShowScreen/Page/Coords -> tooltip.js show()).
      core::TooltipRowFlags flags;
      flags.showScreen = tooltipShowScreen_;
      flags.showPage = tooltipShowPage_;
      flags.showCoords = tooltipShowCoords_;
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

    // Nearest point within (markerSize + 6)/scale image px.
    const core::Point* nearest = nullptr;
    double bestD = 1e18;
    for (const auto& line : all) {
      const double thresh = (line.markerSize + 6.0) / scale;
      for (const auto& p : line.points) {
        const double d = std::hypot(imageX - p.x, imageY - p.y);
        if (d <= thresh && d < bestD) {
          bestD = d;
          nearest = &p;
        }
      }
    }
    if (nearest) {
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
  // Export the current layout as pretty JSON to a file. Guards "no lines" like
  // the browser (drawingApp.js downloadJSON ~2071-2090).
  void MainWindow::downloadLayout() {
    if (canvas_->allLines().empty()) {
      notify_->error("No lines to export");  // drawingApp.js:2073 alert
      return;
    }
    const QString suggested = projectBaseName() + "-layout.json";
    const QString path = QFileDialog::getSaveFileName(
        this, "Export layout JSON", suggested, "JSON (*.json)");
    if (path.isEmpty()) return;
    const QJsonObject obj = fileStore::buildLayoutJson(
        canvas_->imageWidth(), canvas_->imageHeight(), canvas_->allLines(),
        settings_.imageFilter, settings_.filterColor);
    // Indented, matching the browser's JSON.stringify(data, null, 2).
    const QByteArray bytes =
        QJsonDocument(obj).toJson(QJsonDocument::Indented);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      notify_->error("Could not write file");
      return;
    }
    f.write(bytes);
    f.close();
    notify_->success("Layout exported");
  }

  // Import a layout JSON file and adopt it (with the confirm/dimension guards in
  // applyLayoutJson). Mirrors browser uploadJSON (drawingApp.js ~2092-2130).
  void MainWindow::uploadLayout() {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");  // drawingApp.js:2102
      return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, "Import layout JSON", QString(), "JSON (*.json)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      notify_->error("Could not read file");
      return;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
      notify_->error("Error loading JSON: " + err.errorString());
      return;
    }
    applyLayoutJson(doc.object());
  }

  // Copy the layout JSON text to the clipboard. Guards "no layout" like the
  // browser (drawingApp.js copyLayoutToClipboard ~2181-2196).
  void MainWindow::copyLayout() {
    if (canvas_->allLines().empty()) {
      notify_->error("No layout to copy");  // drawingApp.js:2183
      return;
    }
    // Copy the FULL layout (Ctrl+Alt+C): lines + filter/tint + crop + rotation +
    // page meta, matching the server-save envelope so every applied edit travels.
    const QJsonObject obj = fileStore::buildLayoutJson(
        canvas_->imageWidth(), canvas_->imageHeight(), canvas_->allLines(),
        settings_.imageFilter, settings_.filterColor,
        canvas_->cropRect(), canvas_->rotationQuarters(),
        currentLayoutMeta());
    const QByteArray txt =
        QJsonDocument(obj).toJson(QJsonDocument::Indented);
    QGuiApplication::clipboard()->setText(QString::fromUtf8(txt));
    notify_->success("Layout JSON copied");
  }

  // Parse clipboard text as a layout JSON object and adopt it. Mirrors the
  // text branch of the browser paste listener (drawingApp.js :582-591).
  void MainWindow::pasteLayout() {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");
      return;
    }
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) {
      notify_->error("Clipboard has no layout JSON");
      return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject() || !doc.object().value("lines").isArray()) {
      notify_->error("Clipboard has no layout JSON");
      return;
    }
    applyLayoutJson(doc.object());
  }

  // Confirm-replace + dimension-mismatch guard, then adopt the parsed layout.
  // Shared by uploadLayout + pasteLayout, mirroring the browser's uploadJSON /
  // applyPastedLayout flow (drawingApp.js ~2101-2222): replace prompt only when
  // lines already exist, dimension prompt only on mismatch, then setLines +
  // history. setLines emits changed(), so the panel/buttons refresh.
  void MainWindow::applyLayoutJson(const QJsonObject& obj) {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");
      return;
    }
    if (!canvas_->allLines().empty()) {
      if (QMessageBox::question(this, "Replace layout",
                                "Replace current layout with the imported JSON?")
          != QMessageBox::Yes) {
        notify_->info("Import canceled");  // drawingApp.js:2107 "Upload canceled"
        return;
      }
    }
    int w = 0, h = 0;
    core::Lines lines = fileStore::parseLayoutJson(obj, w, h);
    if (w != canvas_->imageWidth() || h != canvas_->imageHeight()) {
      if (QMessageBox::question(this, "Dimension mismatch",
                                "Image dimensions do not match. Continue anyway?")
          != QMessageBox::Yes) {
        notify_->info("Import canceled");  // drawingApp.js:2113 dimension guard
        return;
      }
    }
    canvas_->setLines(lines);  // emits changed() -> refresh panel + buttons
    notify_->success("Layout loaded");
  }

  // Render the canvas (image + filter + overlay) to a file. Extension drives the
  // encoder (jpg/png/webp/bmp; anything else -> png). Mirrors the browser
  // saveImage mime map (drawingApp.js :2062-2068) but writes to a chosen path.
  void MainWindow::saveImageFile() {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");  // drawingApp.js:2037 "No image"
      return;
    }
    const QString suggested = projectBaseName() + "-drawing." +
                              canvas_->imageExt();
    const QString path = QFileDialog::getSaveFileName(
        this, "Save image", suggested,
        "Images (*.png *.jpg *.jpeg *.webp *.bmp)");
    if (path.isEmpty()) return;
    // Map the chosen extension to a Qt encoder format; default png (matching the
    // browser's mimeMap fallback, drawingApp.js:2063-2064).
    const QString ext = QFileInfo(path).suffix().toLower();
    const char* fmt = "PNG";
    if (ext == "jpg" || ext == "jpeg") fmt = "JPG";
    else if (ext == "webp") fmt = "WEBP";
    else if (ext == "bmp") fmt = "BMP";
    else if (ext == "png") fmt = "PNG";
    // withOverlay=true: bake the points/lines onto the saved image.
    if (canvas_->renderToImage(true).save(path, fmt)) {
      notify_->success("Image saved");
    } else {
      notify_->error("Could not save image");
    }
  }

  // Copy the filtered image (no overlay) to the clipboard. Mirrors the browser
  // copyImageToClipboard, which draws image+filter only (drawingApp.js
  // :2133-2152, drawImageWithFilter — no lines/points).
  void MainWindow::copyImageToClipboard() {
    if (!canvas_->hasImage()) {
      notify_->error("No image to copy");  // drawingApp.js:2134
      return;
    }
    QGuiApplication::clipboard()->setImage(canvas_->renderToImage(false));
    notify_->success("Image copied to clipboard");
  }

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
      canvas_->loadFromImage(img);
      refreshActions();
      notify_->success("Image pasted from clipboard");
      return;
    }
    // No image — try a layout JSON text payload (drawingApp.js :582-591).
    pasteLayout();
  }

  // ── view / zoom ──
  void MainWindow::zoomStep(int dir) {
    setZoom(canvas_->scale() * (dir > 0 ? 1.25 : 0.8));
  }
  void MainWindow::zoomIn() { setZoom(canvas_->scale() * 1.25); }
  void MainWindow::zoomOut() { setZoom(canvas_->scale() * 0.8); }

  void MainWindow::setZoom(double scale, bool syncCombo) {
    scale = std::clamp(scale, 0.05, 5.0);  // LIMITS.zoomMin / zoomMax
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

  void MainWindow::fitToWindow() {
    if (!canvas_->hasImage()) return;
    const QSize vp = scroll_->viewport()->size();
    const double sx = double(vp.width()) / canvas_->imageWidth();
    const double sy = double(vp.height()) / canvas_->imageHeight();
    setZoom(std::min(sx, sy) * 0.95);
  }

  void MainWindow::toggleFullscreen() {
    if (isFullScreen()) showNormal();
    else showFullScreen();
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
    if (mods & (Qt::AltModifier | Qt::ControlModifier | Qt::MetaModifier)) {
      QMainWindow::keyPressEvent(event);
      return;
    }
    const int step = (mods & Qt::ShiftModifier) ? 22 : 7;
    int dx = 0;
    int dy = 0;
    const int key = event->key();
    if (key == Qt::Key_Left) {
      dx = -step;
    } else if (key == Qt::Key_Right) {
      dx = step;
    } else if (key == Qt::Key_Up) {
      dy = -step;
    } else if (key == Qt::Key_Down) {
      dy = step;
    } else {
      QMainWindow::keyPressEvent(event);
      return;
    }
    scrollTo(scroll_->horizontalScrollBar()->value() + dx,
             scroll_->verticalScrollBar()->value() + dy);
    event->accept();
  }

  // ── theme + settings ──
  void MainWindow::applyTheme() {
    // Tri-state resolution (S14): system follows the OS scheme.
    const bool dark = resolveDark(settings_.themeMode);
    // Apply at the application level so menus, popups and native chrome (which
    // aren't children of this window) are themed too. With the Fusion style set
    // in main(), a matching palette + stylesheet themes the whole app — on
    // Fedora a widget-level setStyleSheet left the menubar/toolbar unthemed.
    qApp->setPalette(buildQPalette(dark, settings_.accentColor));
    qApp->setStyleSheet(buildStylesheet(dark, settings_.accentColor));
    canvas_->setDark(dark);
    canvas_->setAccent(settings_.accentColor);
    incognitoOverlay_->setTheme(dark, settings_.accentColor);
    actTheme_->setText(dark ? "Light Theme" : "Dark Theme");

    // Re-tint the shared line-art icons to the active text color (light/dark/accent).
    const QColor iconCol = themePalette(dark, settings_.accentColor).textMain;
    styleActionIcons(dark, iconCol);
    if (selPanel_) selPanel_->restyleIcons(iconCol);

    QPalette vp;
    vp.setColor(QPalette::Window, themePalette(dark).bgPage);
    scroll_->viewport()->setAutoFillBackground(true);
    scroll_->viewport()->setPalette(vp);
  }

  // Map every action + icon toolbutton to a shared-icon glyph rasterized in
  // `iconColor` (names mirror browser/js/ui/toolbar.js). Null-guarded.
  void MainWindow::styleActionIcons(bool dark, const QColor& iconColor) {
    iconColor_ = iconColor;
    const int s = 18;
    auto set = [&](QAction* a, const char* name) {
      if (a) a->setIcon(themedIcon(QString::fromLatin1(name), iconColor, s));
    };
    // File / image
    set(actOpen_, "image");
    set(actOpenAnother_, "external");
    set(actNewBlank_, "plus-circle");
    set(actLinks_, "link");
    set(actConnect_, "server");
    set(actCrop_, "crop");
    set(actRotateLeft_, "rotate-ccw");
    set(actRotateRight_, "rotate-cw");
    set(actCycleFilter_, "image");
    // Drawing / history
    set(actStartDraw_, "play");
    set(actStopDraw_, "stop");
    set(actNewLine_, "plus");
    set(actUndo_, "undo");
    set(actRedo_, "redo");
    set(actDeleteLast_, "minus");
    set(actDeleteLine_, "trash");
    set(actDeletePoint_, "x");
    set(actClearAll_, "trash");
    set(actDeselect_, "x");
    // View / zoom
    set(actZoomIn_, "plus");
    set(actZoomOut_, "minus");
    set(actFit_, "fit");
    set(actShowPoints_, "eye");
    set(actShowLines_, "eye");
    set(actPanel_, "layers");
    set(actFullscreen_, "maximize");
    set(actTooltip_, "message");
    set(actAllowFormulas_, "function");
    set(actUnitCm_, "ruler");
    set(actUnitIn_, "ruler");
    set(actIncognito_, "incognito");
    set(actSettings_, "gear");
    // Project / data
    set(actProjects_, "folder");
    set(actNewProject_, "file-text");
    set(actSaveProject_, "save");
    set(actSaveSession_, "clipboard");
    set(actDownloadJson_, "download");
    set(actUploadJson_, "upload");
    set(actCopyLayout_, "copy");
    set(actPasteLayout_, "paste");
    set(actSaveImage_, "image");
    set(actCopyImage_, "copy");
    set(actPasteImage_, "paste");
    // Help
    set(actInfo_, "info");
    set(actShortcuts_, "help");
    set(actQuit_, "power");
    // Context-menu extras
    set(actDrawModeToggle_, "pencil");
    set(actDrawRectNow_, "rect-filled");
    // The theme toggle shows the destination scheme (sun when dark, moon when light),
    // matching the browser's toggle glyph.
    if (actTheme_) actTheme_->setIcon(themedIcon(dark ? "sun" : "moon", iconColor, s));

    // Toolbuttons that aren't backed by a QAction. The rename confirm/cancel mirror the browser's
    // green ✓ / red ✗ inline-edit buttons.
    if (projectNameAccept_)
      projectNameAccept_->setIcon(themedIcon("check", QColor("#2e9e4f"), 16));
    if (projectNameCancel_)
      projectNameCancel_->setIcon(themedIcon("x", QColor("#d6293e"), 16));
    // Browser-style name affordances: a ✎ rename pencil + a 🎨 colour icon (flat line-art glyphs
    // following the theme text colour — not a filled swatch).
    if (projectNameEdit_) projectNameEdit_->setIcon(themedIcon("pencil", iconColor, 15));
    if (projectColorBtn_) projectColorBtn_->setIcon(themedIcon("palette", iconColor, 15));
    if (drawModeBtn_) {
      const bool rect =
          canvas_ && canvas_->drawMode() == CanvasWidget::DrawMode::Rect;
      drawModeBtn_->setIcon(
          themedIcon(rect ? "rect-filled" : "pencil", iconColor, 16));
    }
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

  // Ctrl+D sets an explicit light/dark and stops following the OS (browser
  // behavior: a manual toggle overrides the system preference).
  void MainWindow::toggleTheme() {
    settings_.themeMode = resolveDark(settings_.themeMode) ? "light" : "dark";
    applySettings(settings_, true);
    notify_->info(settings_.themeMode == "dark" ? "Dark theme" : "Light theme");
  }

  void MainWindow::applySettings(const Settings& s, bool persist) {
    settings_ = s;
    canvas_->setDefaults(s.defaultColor, s.defaultThickness, s.defaultMarkerSize,
                         s.defaultStyle);
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
    canvas_->setShowPoints(s.showPoints);
    canvas_->setShowLines(s.showLines);
    {
      QSignalBlocker b(pageSize_);
      pageSize_->setCurrentText(s.pageSize);
    }
    // Sync custom page-size inputs (S10) in the active display unit.
    if (customW_) {
      applyUnitToPageInputs();
      if (customGroupAct_) customGroupAct_->setVisible(s.pageSize == "custom");
    }
    // Sync formula controls (S11).
    if (allowFormulas_) {
      QSignalBlocker ba(allowFormulas_);
      QSignalBlocker bx(formulaX_);
      QSignalBlocker by(formulaY_);
      allowFormulas_->setChecked(s.allowFormulas);
      formulaX_->setText(s.formulaX);
      formulaY_->setText(s.formulaY);
      if (formulaGroupAct_) formulaGroupAct_->setVisible(s.allowFormulas);
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
    if (lineColorBtn_) updateColorSwatch(lineColorBtn_, lineColorValue_);
    if (filterColorBtn_) updateColorSwatch(filterColorBtn_, filterColorValue_);
    if (lineThickness_) {
      QSignalBlocker bt(lineThickness_);
      lineThickness_->setValue(qRound(s.defaultThickness));
    }
    if (markerSize_) {
      QSignalBlocker bm(markerSize_);
      markerSize_->setValue(qRound(s.defaultMarkerSize));
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
    if (filterColorAct_) filterColorAct_->setVisible(s.imageFilter == "custom");
    canvas_->setImageFilter(s.imageFilter, filterColorValue_);
    applyTheme();
    // S6: even an explicit Settings-dialog save is suppressed in incognito.
    if (persist && !incognito_) fileStore::saveSettings(settings_);
  }

  void MainWindow::openSettings() {
    SettingsDialog dlg(settings_, this);
    if (dlg.exec() == QDialog::Accepted) {
      applySettings(dlg.result(), true);
      notify_->success("Settings saved");
    }
  }

  // ── persistence ──
  // Incognito (S6) gates every write of the incognito editor's OWN state —
  // its session autosave, its settings, its project promotion/save, and the
  // shortcut overrides. This is a deliberate desktop-only extension: the
  // browser's incognito only skips the session + project promotion, but the
  // desktop also freezes settings + shortcut writes while incognito is on.
  // It does NOT gate maintenance on OTHER saved projects (explicit delete and
  // the expiry sweep in openProjects) — see the notes at those call sites.
  void MainWindow::scheduleAutosave() {
    if (incognito_) return;  // S6: no autosave timer while incognito
    if (settings_.autosave) autosaveTimer_->start(600);
  }

  void MainWindow::saveSessionNow() {
    if (incognito_) return;  // S6: skip session writes while incognito
    // Sync off + a fetched server project = edit-in-memory only: don't persist the
    // restore blob either (the session is "stored nowhere").
    if (!remoteAddress_.isEmpty() && !settings_.syncToServer) return;
    Session s;
    s.imagePath = canvas_->imagePath();
    s.pageSize = pageSize_->currentText();
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
    {
      QSignalBlocker b(pageSize_);
      pageSize_->setCurrentText(sess->pageSize);
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
    if (filterColorAct_)
      filterColorAct_->setVisible(sess->imageFilter == "custom");
    canvas_->setImageFilter(sess->imageFilter, filterColorValue_);
    canvas_->setDrawMode(sess->drawMode == "rect"
                             ? CanvasWidget::DrawMode::Rect
                             : CanvasWidget::DrawMode::Line);
    setZoom(sess->scale);
    notify_->info("Restored last session");
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
      if (!mgr->connectTo(srv.url, srv.token, err)) ++failed;  // a dead server stays absent
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

  void MainWindow::openConnections() {
    ConnectDialog dlg(ensureConnections(), this);
    dlg.exec();
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

    ProjectsDialog dlg(projectList_, nowMs(), connections_, buildProjectThumbs(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    using Action = ProjectsDialog::Action;
    if (dlg.action() == Action::Open) {
      loadProjectIntoCanvas(dlg.selectedId());
    } else if (dlg.action() == Action::OpenRemote) {
      openServerProject(dlg.selectedServerUrl(), dlg.selectedId());
    } else if (dlg.action() == Action::OpenInNewWindow) {
      openProjectInNewWindow(dlg.selectedId());
    } else if (dlg.action() == Action::MoveToServer) {
      // Can't move a project that's open in another window — it would vanish there.
      if (projectOpenInOtherWindow(dlg.selectedId())) {
        notify_->error("That project is open in another window — close it there first");
        return;
      }
      moveLocalProjectToServer(dlg.selectedServerUrl(), dlg.selectedId());
    } else if (dlg.action() == Action::CopyToServer) {
      copyLocalProjectToServer(dlg.selectedServerUrl(), dlg.selectedId(), dlg.newName());
    } else if (dlg.action() == Action::MoveToLocal) {
      // Move-to-local is allowed even if a peer/other client has the project open (the
      // server delete just ends their live link — they keep their in-memory copy).
      moveServerProjectToLocal(dlg.selectedServerUrl(), dlg.selectedId());
    } else if (dlg.action() == Action::MakeLocalCopy) {
      makeLocalCopyOfServerProject(dlg.selectedServerUrl(), dlg.selectedId(), dlg.newName());
    } else if (dlg.action() == Action::BatchMoveToServer) {
      for (const auto& pr : dlg.batchItems()) moveLocalProjectToServer(dlg.selectedServerUrl(), pr.first);
    } else if (dlg.action() == Action::BatchCopyToServer) {
      for (const auto& pr : dlg.batchItems()) copyLocalProjectToServer(dlg.selectedServerUrl(), pr.first, QString());
    } else if (dlg.action() == Action::BatchMoveToLocal) {
      for (const auto& pr : dlg.batchItems()) moveServerProjectToLocal(pr.second, pr.first);
    } else if (dlg.action() == Action::BatchCopyToLocal) {
      // Bulk copy without opening each (empty name → keeps the server project's name).
      for (const auto& pr : dlg.batchItems()) {
        QString nid;
        importServerProjectToLocal(pr.second, pr.first, /*removeFromServer=*/false, QString(), &nid);
      }
      refreshActions();
      refreshDockMenu();
      notify_->success(QString("Made %1 local copy(ies)").arg(dlg.batchItems().size()));
    } else if (dlg.action() == Action::BatchRemove) {
      const auto items = dlg.batchItems();
      if (QMessageBox::question(
              this, "Remove projects",
              QString("Remove %1 selected project(s)? Server projects are deleted from the server.")
                  .arg(items.size()),
              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
      for (const auto& pr : items) {
        const QString id = pr.first;
        const QString server = pr.second;
        if (server.isEmpty()) {
          const std::string sid = id.toStdString();
          projectList_.erase(
              std::remove_if(projectList_.begin(), projectList_.end(),
                             [&](const Project& p) { return p.meta.id == sid; }),
              projectList_.end());
          if (activeProjectId_ == id) activeProjectId_.clear();
        } else if (auto* c = connections_ ? connections_->find(server) : nullptr) {
          c->deleteProject(id);
        }
      }
      fileStore::saveProjects(projectList_);
      refreshActions();
      refreshDockMenu();
    } else if (dlg.action() == Action::ClearAll) {
      // Remove ALL local projects (server projects are untouched), mirroring the browser modal's
      // "Clear All". Confirmed because it's destructive.
      const int n = static_cast<int>(projectList_.size());
      if (n == 0) {
        notify_->info("No local projects to clear");
        return;
      }
      if (QMessageBox::question(
              this, "Clear all projects",
              QString("Remove all %1 local project(s)? This cannot be undone. Server projects are not affected.")
                  .arg(n),
              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
      projectList_.clear();
      activeProjectId_.clear();
      fileStore::saveProjects(projectList_);
      refreshActions();
      refreshDockMenu();
      notify_->success(QString("Cleared %1 local project(s)").arg(n));
    } else if (dlg.action() == Action::Delete) {
      // Block removing a project that's open in another window (matches the browser's
      // "open in another tab" guard).
      if (projectOpenInOtherWindow(dlg.selectedId())) {
        notify_->error("That project is open in another window — close it there first");
        return;
      }
      // Confirm the destructive remove (the browser modal asks too).
      const Project* pr = findProject(dlg.selectedId().toStdString());
      const QString nm = pr ? QString::fromStdString(pr->meta.name) : QStringLiteral("this project");
      if (QMessageBox::question(
              this, "Remove project",
              QString("Remove \"%1\"? This cannot be undone.").arg(nm),
              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
      const std::string id = dlg.selectedId().toStdString();
      projectList_.erase(
          std::remove_if(projectList_.begin(), projectList_.end(),
                         [&](const Project& p) { return p.meta.id == id; }),
          projectList_.end());
      if (activeProjectId_ == dlg.selectedId()) activeProjectId_.clear();
      // Not gated by incognito: operates on other saved projects, not the
      // incognito editor's content (see S6 scope note above).
      fileStore::saveProjects(projectList_);
      refreshActions();
      refreshDockMenu();  // drop it from the Dock "recent" list
      notify_->info("Project deleted");
    } else if (dlg.action() == Action::SetColor) {
      // Set/clear a project's accent colour (local meta or server PUT), then repaint
      // the active name if it's the one that changed.
      if (setProjectColorById(dlg.selectedId(), dlg.selectedServerUrl(), dlg.selectedColor())) {
        if (dlg.selectedServerUrl().isEmpty() && activeProjectId_ == dlg.selectedId())
          updateProjectTitle();
        else if (!dlg.selectedServerUrl().isEmpty() && remoteId_ == dlg.selectedId()
                 && remoteAddress_ == dlg.selectedServerUrl()) {
          remoteColor_ = normalizeProjectColor(dlg.selectedColor()).value_or(QString());
          updateProjectTitle();
        }
      }
    } else if (dlg.action() == Action::Rename) {
      // The dialog already validated, but re-validate here so any rename path is safe.
      renameProjectById(dlg.selectedId(), dlg.newName());
    } else if (dlg.action() == Action::Renew) {
      Project* pr = findProject(dlg.selectedId().toStdString());
      if (!pr) return;
      // Restart the 7-day expiry window from now without touching content.
      pr->meta.updatedAt = nowMs();
      // Not gated by incognito: operates on other saved projects, not the
      // incognito editor's content (see S6 scope note above).
      fileStore::saveProjects(projectList_);
      notify_->success(QString("Renewed \"%1\" — expires in 7 days")
                           .arg(QString::fromStdString(pr->meta.name)));
    } else if (dlg.action() == Action::New) {
      if (incognito_) {  // S6: no project promotion while incognito
        notify_->info("Incognito mode — saving is disabled");
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
    const core::PageSize page = naturalPageCm(pageSize_->currentText(),
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
      const core::PageSize page = naturalPageCm(pageSize_->currentText(),
                                                settings_.customPageWidth,
                                                settings_.customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    canvas_->restore(pr->imagePath, pr->lines, canvas_->scale(), pr->cropRect,
                     pr->rotationQuarters);
    activeProjectId_ = id;
    remoteAddress_.clear();  // a local project is not server-linked
    remoteId_.clear();
    remoteName_.clear();
    remoteColor_.clear();
    remoteVersion_ = 0;
    stopRemotePoll();   // no longer a server session
    currentSource_ = QString::fromStdString(pr->meta.source);
    currentResource_ = QString::fromStdString(pr->meta.resource);
    refreshActions();
    notify_->success(
        QString("Opened \"%1\"").arg(QString::fromStdString(pr->meta.name)));
    return true;
  }

  // Open a server-stored project: fetch its record (name/version/layout) + the
  // original image bytes, load them onto this canvas, and link the session so a
  namespace {
    // A stable value key for a line, for dedup when merging two editors' layouts on a
    // save conflict (mirrors the browser's JSON-stringify dedup in mergeLines).
    QString lineKey(const core::Line& l) {
      QString k = QString("%1|%2|%3|%4|%5|%6")
                      .arg(QString::fromStdString(l.color))
                      .arg(l.thickness).arg(l.markerSize)
                      .arg(QString::fromStdString(l.style))
                      .arg(l.locked ? 1 : 0)
                      .arg(QString::fromStdString(l.fillColor));
      for (const auto& p : l.points) k += QString(";%1,%2").arg(p.x).arg(p.y);
      return k;
    }

    // Synchronously GET an http(s) URL's bytes (no auth), with a timeout. Used to open
    // a server project whose image lives only at its `source` web URL (an
    // extension-added project that never uploaded bytes). One user-triggered open, so
    // blocking briefly is fine; the timeout guards against an unreachable host.
    QByteArray fetchUrlBytes(const QString& url) {
      const QUrl u(url);
      if (!u.isValid() || (u.scheme() != "http" && u.scheme() != "https")) return {};
      QNetworkAccessManager nam;
      QNetworkRequest req(u);
      req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
      QNetworkReply* reply = nam.get(req);
      QEventLoop loop;
      QTimer timeout;
      timeout.setSingleShot(true);
      QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
      QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
      timeout.start(10000);
      loop.exec();
      QByteArray out;
      if (reply->isFinished() && reply->error() == QNetworkReply::NoError)
        out = reply->readAll();
      reply->abort();
      reply->deleteLater();
      return out;
    }

    // Sets a bool flag true for the current scope, clearing it on exit — re-entrancy
    // guard for the remote reload/push paths.
    struct ScopedFlag {
      bool& f;
      explicit ScopedFlag(bool& b) : f(b) { f = true; }
      ~ScopedFlag() { f = false; }
    };

    // Read a layout's saved filter/tint (legacy blackAndWhite → "bw"); an absent or empty
    // layout yields "none" + the default tint.
    void parseLayoutFilter(const QJsonObject& layout, const QString& defTint,
                           QString& filter, QString& tint) {
      filter = layout.value("imageFilter")
                   .toString(layout.value("blackAndWhite").toBool(false) ? "bw" : "none");
      tint = layout.value("filterColor").toString(defTint);
    }
  }  // namespace

  // later Save writes back. Mirrors the browser projectsModal openRemote().
  bool MainWindow::openServerProject(const QString& serverUrl, const QString& id, bool silent) {
    if (!connections_) return false;
    // Loading the canvas below emits changed() — guard so it isn't taken for a user
    // edit and pushed straight back (feedback loop).
    ScopedFlag reloadGuard{remoteReloading_};
    stencil::net::ServerClient* c = connections_->find(serverUrl);
    if (!c) {
      notify_->error("Not connected to that server");
      return false;
    }
    stencil::net::ServerProject meta;
    QJsonObject layout;
    if (!c->getProject(id, meta, layout)) {
      notify_->error(QString("Could not open server project — %1").arg(c->lastError()));
      return false;
    }
    bool ok = false;
    QByteArray bytes = c->downloadFile(id, "original", ok);
    if (!ok || bytes.isEmpty()) {
      // No stored bytes on the server (e.g. an extension-added project that only
      // recorded the image's web URL) — fetch that source URL directly. Qt Network
      // has no browser-style CORS limit.
      bytes = fetchUrlBytes(meta.source);
      if (bytes.isEmpty()) {
        notify_->error(QString("Could not download image — %1").arg(c->lastError()));
        return false;
      }
    }
    QImage img;
    if (!img.loadFromData(bytes)) {
      notify_->error("Server image could not be decoded");
      return false;
    }
    // Adopt the project's page format + formulas before sizing the canvas page below.
    adoptServerLayoutMeta(layout);
    {
      const core::PageSize page = naturalPageCm(pageSize_->currentText(),
                                                settings_.customPageWidth,
                                                settings_.customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    // Restore the project's geometry (rotation + crop) from the layout, then adopt its
    // lines. Rotation applies before the crop (the crop lives in rotated-original space);
    // an empty/old layout default-crops and stays un-rotated.
    int lw = 0, lh = 0;
    core::CropRect crop;
    int rot = 0;
    core::Lines lines = fileStore::parseLayoutJson(layout, lw, lh, &crop, &rot);
    canvas_->loadFromImage(img, crop, rot);
    if (!lines.empty()) canvas_->setLines(lines);
    // Restore the project's saved filter/tint (an empty layout resets to "none" + the
    // default tint, so the previously-open project's filter doesn't bleed onto this image).
    QString filter, tint;
    parseLayoutFilter(layout, settings_.filterColor, filter, tint);
    applyTintColor(QColor(tint));
    applyImageFilter(filter);
    // Link the session; clear any local-project linkage so saves go to the server.
    activeProjectId_.clear();
    remoteAddress_ = serverUrl;
    remoteId_ = id;
    remoteName_ = meta.name;
    remoteColor_ = meta.color;
    remoteVersion_ = meta.version;
    currentSource_ = meta.source;
    currentResource_ = meta.resource;
    filterDirty_ = false;   // we just adopted the server/project filter
    refreshActions();
    startRemotePoll();   // live co-edit: watch for peers changing this project
    if (!silent)
      notify_->success(QString("Opened \"%1\" from %2")
                           .arg(meta.name.isEmpty() ? QStringLiteral("Untitled") : meta.name,
                                serverUrl));
    return true;
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
        const int idx = pageSize_->findText(m.pageSize);
        if (idx >= 0) pageSize_->setCurrentIndex(idx);
      }
      settings_.pageSize = pageSize_->currentText();
      if (customGroupAct_) customGroupAct_->setVisible(settings_.pageSize == "custom");
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
      if (formulaGroupAct_) formulaGroupAct_->setVisible(allow);
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
      win->close();
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

  // Gather a local project's original image bytes + dimensions: from the live canvas when it's
  // the active project (latest edits), else from its stored image file. False + notify when
  // there's no usable image.
  bool MainWindow::localProjectOriginal(const Project& pr, QByteArray& bytes, QString& ext,
                                        int& w, int& h) {
    ext = "png";
    if (QString::fromStdString(pr.meta.id) == activeProjectId_ && canvas_->hasImage()) {
      const QImage img = canvas_->image();
      bytes = pngBytes(img);
      w = img.width();
      h = img.height();
      return true;
    }
    if (pr.imagePath.isEmpty()) {
      notify_->error("This project has no stored image");
      return false;
    }
    const QImage img(pr.imagePath);
    if (img.isNull()) {
      notify_->error("Could not read the project image");
      return false;
    }
    w = img.width();
    h = img.height();
    QFile f(pr.imagePath);
    if (f.open(QIODevice::ReadOnly)) {
      bytes = f.readAll();
      f.close();
      const QString suf = QFileInfo(pr.imagePath).suffix().toLower();
      if (!suf.isEmpty()) ext = suf;
    }
    if (bytes.isEmpty()) bytes = pngBytes(img);  // unreadable file → re-encode the decoded image
    return true;
  }

  // Create `pr` on the server under `name`: upload the original bytes, then push the annotated
  // layout (lines + filter + page/formulas) so the server holds the full project. Reports the
  // new id/version via out-params; false + notify on failure.
  bool MainWindow::createServerFromLocal(stencil::net::ServerClient* c, const Project& pr,
                                         const QString& name, const QByteArray& bytes,
                                         const QString& ext, int w, int h,
                                         QString& newIdOut, qint64& newVersionOut) {
    QString newId;
    qint64 version = 0;
    if (!c->createProject(name, QString::fromStdString(pr.meta.source),
                          QString::fromStdString(pr.meta.resource), true, w, h, newId, version)) {
      notify_->error(QString("Could not create on server — %1").arg(c->lastError()));
      return false;
    }
    if (!c->uploadFile(newId, "original", bytes, ext, w, h)) {
      notify_->error(QString("Created, but image upload failed — %1").arg(c->lastError()));
      return false;
    }
    const QJsonObject layout = fileStore::buildLayoutJson(
        w, h, pr.lines, settings_.imageFilter, settings_.filterColor,
        pr.cropRect, pr.rotationQuarters, currentLayoutMeta());
    qint64 newVersion = version;
    bool conflict = false;
    c->updateProject(newId, name, layout, version, newVersion, conflict);
    newIdOut = newId;
    newVersionOut = newVersion;
    return true;
  }

  // Local → server: create the project on `serverUrl`, upload its original image, push
  // the annotated layout, then drop the local copy. Mirrors the browser's
  // moveProjectToServer() (drawingApp.js).
  void MainWindow::moveLocalProjectToServer(const QString& serverUrl, const QString& id) {
    stencil::net::ServerClient* c = connections_ ? connections_->find(serverUrl) : nullptr;
    if (!c) {
      notify_->error("Not connected to that server");
      return;
    }
    Project* pr = findProject(id.toStdString());
    if (!pr) {
      notify_->error("Project not found");
      return;
    }
    QByteArray bytes;
    QString ext;
    int w = 0;
    int h = 0;
    if (!localProjectOriginal(*pr, bytes, ext, w, h)) return;
    const QString name = QString::fromStdString(pr->meta.name);
    // Carry the project's accent colour onto the server copy (create can't set it).
    const QString localColor = QString::fromStdString(pr->meta.color);
    QString newId;
    qint64 newVersion = 0;
    if (!createServerFromLocal(c, *pr, name, bytes, ext, w, h, newId, newVersion)) return;
    if (!localColor.isEmpty()) {
      bool colorConflict = false;
      c->updateProjectColor(newId, localColor, newVersion, newVersion, colorConflict);
    }
    // The local copy is now redundant — remove it.
    const std::string sid = id.toStdString();
    const bool wasActive = (activeProjectId_ == id);
    projectList_.erase(
        std::remove_if(projectList_.begin(), projectList_.end(),
                       [&](const Project& p) { return p.meta.id == sid; }),
        projectList_.end());
    fileStore::saveProjects(projectList_);
    // If it was the open project, keep the editor open and LINK the live session to the new
    // server project (golden frame) instead of orphaning the canvas.
    if (wasActive) {
      activeProjectId_.clear();
      remoteAddress_ = serverUrl;
      remoteId_ = newId;
      remoteName_ = name;
      remoteColor_ = localColor;
      remoteVersion_ = newVersion;
      startRemotePoll();
      updateProjectTitle();
    }
    refreshActions();
    refreshDockMenu();
    notify_->success(QString("Moved \"%1\" to %2").arg(name, serverUrl));
  }

  // Local → server COPY: create a new server project from a local one (default name
  // "<name>-copy"), leaving the local project in place. Mirrors browser copyProjectToServer.
  void MainWindow::copyLocalProjectToServer(const QString& serverUrl, const QString& id,
                                            const QString& name) {
    stencil::net::ServerClient* c = connections_ ? connections_->find(serverUrl) : nullptr;
    if (!c) {
      notify_->error("Not connected to that server");
      return;
    }
    Project* pr = findProject(id.toStdString());
    if (!pr) {
      notify_->error("Project not found");
      return;
    }
    QByteArray bytes;
    QString ext;
    int w = 0;
    int h = 0;
    if (!localProjectOriginal(*pr, bytes, ext, w, h)) return;
    const QString copyName = name.trimmed().isEmpty()
                                 ? (QString::fromStdString(pr->meta.name) + "-copy")
                                 : name.trimmed();
    QString newId;
    qint64 newVersion = 0;
    if (!createServerFromLocal(c, *pr, copyName, bytes, ext, w, h, newId, newVersion)) return;
    refreshActions();
    refreshDockMenu();
    notify_->success(QString("Copied \"%1\" to %2").arg(copyName, serverUrl));
  }

  // Server → local: download the project's image + layout, persist it as a new local
  // project, then delete it from the server. Mirrors moveProjectToLocal().
  void MainWindow::moveServerProjectToLocal(const QString& serverUrl, const QString& id) {
    // If this server project is the open remote session, follow it to local so the editor
    // stays open + focused instead of pointing at the deleted server id.
    const bool wasOpen = (remoteId_ == id && remoteAddress_ == serverUrl);
    QString newId;
    if (!importServerProjectToLocal(serverUrl, id, /*removeFromServer=*/true, "", &newId))
      return;
    if (wasOpen) loadProjectIntoCanvas(newId);   // rebind the editor to the new local project
    refreshActions();
    refreshDockMenu();
    notify_->success("Moved to local storage");
  }

  void MainWindow::makeLocalCopyOfServerProject(const QString& serverUrl, const QString& id,
                                                const QString& name) {
    QString newId;
    if (!importServerProjectToLocal(serverUrl, id, /*removeFromServer=*/false, name, &newId))
      return;
    refreshActions();
    refreshDockMenu();
    loadProjectIntoCanvas(newId);   // open the detached copy (clears the remote link)
    notify_->success("Local copy created");
  }

  // Shared body: fetch a server project's image + layout (incl. crop/rotation), persist a
  // fresh detached local project; optionally delete the server copy. `name` (when non-empty)
  // overrides the server's name (used for the copy's "<name>-copy"). Errors are reported.
  bool MainWindow::importServerProjectToLocal(const QString& serverUrl, const QString& id,
                                              bool removeFromServer, const QString& name,
                                              QString* newIdOut) {
    stencil::net::ServerClient* c = connections_ ? connections_->find(serverUrl) : nullptr;
    if (!c) {
      notify_->error("Not connected to that server");
      return false;
    }
    stencil::net::ServerProject meta;
    QJsonObject layout;
    if (!c->getProject(id, meta, layout)) {
      notify_->error(QString("Could not fetch server project — %1").arg(c->lastError()));
      return false;
    }
    bool ok = false;
    QByteArray bytes = c->downloadFile(id, "original", ok);
    if (!ok || bytes.isEmpty()) bytes = fetchUrlBytes(meta.source);  // extension-added: only a web URL
    if (bytes.isEmpty()) {
      notify_->error("Server project has no image");
      return false;
    }
    QImage img;
    if (!img.loadFromData(bytes)) {
      notify_->error("Server image could not be decoded");
      return false;
    }
    // Persist the bytes to a file under the state dir so the local project reloads its
    // pixels on open (local projects reference an on-disk imagePath).
    Project pr;
    pr.meta.id = projectsStore_.createId(nowMs(), makeSalt());
    const QString imgDir = fileStore::stateDir() + "/images";
    QDir().mkpath(imgDir);
    const QString path = imgDir + "/" + QString::fromStdString(pr.meta.id) + ".png";
    if (!img.save(path, "PNG")) {
      notify_->error("Could not write the image to local storage");
      return false;
    }
    const QString baseName = meta.name.isEmpty() ? QStringLiteral("Untitled") : meta.name;
    pr.meta.name = (name.trimmed().isEmpty() ? baseName : name.trimmed()).toStdString();
    pr.meta.createdAt = pr.meta.updatedAt = nowMs();
    pr.meta.hasImage = true;
    pr.meta.source = meta.source.toStdString();
    pr.meta.resource = meta.resource.toStdString();
    pr.imagePath = path;
    int lw = 0, lh = 0;
    pr.lines = fileStore::parseLayoutJson(layout, lw, lh, &pr.cropRect, &pr.rotationQuarters);
    projectList_.push_back(pr);
    fileStore::saveProjects(projectList_);
    if (removeFromServer && !c->deleteProject(id))
      notify_->error(QString("Copied locally, but server delete failed — %1").arg(c->lastError()));
    if (newIdOut) *newIdOut = QString::fromStdString(pr.meta.id);
    return true;
  }

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

    // Primary content priority: --project > --src > a bare positional file.
    if (!opts.project.isEmpty()) {
      if (!openProjectByName(opts.project))
        notify_->error(QString("No project named \"%1\"").arg(opts.project));
    } else if (!opts.src.isEmpty()) {
      pendingLaunchLayout_ = opts.layout;  // applied after the image loads
      openImageSource(opts.src, opts.frame);
    } else if (!opts.file.isEmpty()) {
      pendingLaunchLayout_ = opts.layout;
      openPathFromOS(opts.file, opts.frame);
    }

    // --projects: open the Projects window at launch. Queued so it runs after the
    // current call unwinds (and after a primary load has been kicked off).
    if (opts.projects) QTimer::singleShot(0, this, &MainWindow::openProjects);
  }

  // Lazily construct + wire the async --src resolver (image / URL / video frame).
  void MainWindow::ensureMediaLoader() {
    if (mediaLoader_) return;
    mediaLoader_ = new MediaLoader(this);
    connect(mediaLoader_, &MediaLoader::loaded, this,
            &MainWindow::onLaunchImageLoaded);
    connect(mediaLoader_, &MediaLoader::failed, this, [this](const QString& msg) {
      pendingLaunchLayout_.clear();
      pendingProvSource_.clear();
      pendingProvResource_.clear();
      notify_->error(msg);
    });
  }

  void MainWindow::openImageSource(const QString& src, int frame) {
    ensureMediaLoader();
    notify_->info("Opening…");
    mediaLoader_->load(src, frame);
  }

  // Open a file handed in by the OS shell (file-association / "Open With" / drop):
  // a *.json is a layout (applied onto the current image), anything else is an
  // image or video opened via the --src path.
  void MainWindow::openPathFromOS(const QString& path, int frame) {
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().compare("json", Qt::CaseInsensitive) == 0) {
      applyLayoutFromSource(path);
      return;
    }
    openImageSource(path, frame);
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

    const core::PageSize page = naturalPageCm(pageSize_->currentText(),
                                              settings_.customPageWidth,
                                              settings_.customPageHeight);
    canvas_->setPageCm(page.width, page.height);
    bool ok = false;
    if (!localPath.isEmpty())
      ok = canvas_->loadImage(localPath);  // path-backed (keeps it for saves)
    if (!ok) {
      if (image.isNull()) {
        notify_->error("Failed to open the image");
        pendingLaunchLayout_.clear();
        return;
      }
      canvas_->loadFromImage(image);  // remote image / video frame (in-memory)
    }
    // Quick pre-load crop (links modal): override the default page-aspect auto-crop
    // with the chosen page + orientation, or load the full frame uncropped. Applies
    // equally to still images and extracted video frames. Consumed once.
    applyQuickCrop();
    // A new image's provenance replaces the previous one (a plain --src/OS open
    // carries none, so both clear). Saved to the project on the next create/save.
    currentSource_ = provSource;
    currentResource_ = provResource;
    refreshActions();
    fitToWindow();
    notify_->success("Image opened");

    // --layout: apply now that an image exists (applyLayoutJson needs one).
    if (!pendingLaunchLayout_.isEmpty()) {
      const QString src = pendingLaunchLayout_;
      pendingLaunchLayout_.clear();
      applyLayoutFromSource(src);
    }
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
    if (!opts.page.isEmpty()) pageSize_->setCurrentText(opts.page);  // → onPageSizeChanged
    const core::PageSize pg = naturalPageCm(pageSize_->currentText(),
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
      applyLayoutJson(doc.object());
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
  void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    // Accept a dragged local file (image / video / layout JSON); the suffix is
    // resolved on drop by openPathFromOS.
    const QMimeData* m = event->mimeData();
    if (m && m->hasUrls()) {
      for (const QUrl& u : m->urls())
        if (u.isLocalFile()) {
          event->acceptProposedAction();
          return;
        }
    }
  }

  void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* m = event->mimeData();
    if (!m || !m->hasUrls()) return;
    for (const QUrl& u : m->urls()) {
      if (u.isLocalFile()) {
        openPathFromOS(u.toLocalFile());
        event->acceptProposedAction();
        return;  // open the first file only (matches a single-image editor)
      }
    }
  }

  // View/edit/open/remove the current image's source & resource links, or add a
  // new image by URL. Edits to the links persist to the active project (and the
  // in-memory current* provenance); a URL load routes through loadImageByUrl().
  void MainWindow::openLinks() {
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

    LinksDialog dlg(src, res, canvas_->hasImage(), settings_.pageSize, this);
    if (dlg.exec() != QDialog::Accepted) return;

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
    if (incognito_) {  // S6: project promotion is blocked while incognito
      notify_->info("Incognito mode — saving is disabled");
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
  void MainWindow::createLocalProject(const QString& name) {
    remoteAddress_.clear();  // a freshly created local project is not server-linked
    remoteId_.clear();
    remoteName_.clear();
    remoteColor_.clear();
    remoteVersion_ = 0;
    stopRemotePoll();   // no longer a server session
    Project pr;
    pr.meta.id = projectsStore_.createId(nowMs(), makeSalt());
    pr.meta.name = name.toStdString();
    pr.meta.createdAt = pr.meta.updatedAt = nowMs();
    pr.imagePath = canvas_->imagePath();
    pr.lines = canvas_->allLines();
    pr.cropRect = canvas_->cropRect();
    pr.rotationQuarters = canvas_->rotationQuarters();
    pr.meta.hasImage = !pr.imagePath.isEmpty();
    pr.meta.source = currentSource_.toStdString();
    pr.meta.resource = currentResource_.toStdString();
    projectList_.push_back(pr);
    activeProjectId_ = QString::fromStdString(pr.meta.id);
    fileStore::saveProjects(projectList_);
    refreshActions();
    refreshDockMenu();  // surface the new project in the Dock "recent" list
    notify_->success(QString("Created \"%1\"").arg(name));
  }

  // Create the project on `serverUrl` (POST /projects), upload the current image as
  // the 'original', and link the session so a later Save writes back. Mirrors the
  // browser's createRemoteProject (remoteSync.js).
  void MainWindow::createServerProject(const QString& serverUrl, const QString& name) {
    stencil::net::ServerClient* c = connections_ ? connections_->find(serverUrl) : nullptr;
    if (!c) {
      notify_->error("Not connected to that server");
      return;
    }
    const bool hasImage = canvas_->hasImage();
    const int w = hasImage ? canvas_->imageWidth() : 0;
    const int h = hasImage ? canvas_->imageHeight() : 0;
    QString id;
    qint64 version = 0;
    if (!c->createProject(name, currentSource_, currentResource_, hasImage, w, h, id, version)) {
      notify_->error(QString("Could not create on server — %1").arg(c->lastError()));
      return;
    }
    if (hasImage) {
      const QByteArray bytes = pngBytes(canvas_->image());
      if (!c->uploadFile(id, "original", bytes, "png", w, h)) {
        notify_->error(QString("Created, but image upload failed — %1").arg(c->lastError()));
        // The link is still established below so the user can retry via Save.
      } else {
        // The file write bumps the version; re-read it so the next save's guard is
        // accurate (mirrors remoteSync.currentVersion()).
        stencil::net::ServerProject meta;
        QJsonObject lay;
        if (c->getProject(id, meta, lay)) version = meta.version;
      }
    }
    // Link the session; this is now a server project, not a local one.
    activeProjectId_.clear();
    remoteAddress_ = serverUrl;
    remoteId_ = id;
    remoteName_ = name;
    remoteColor_.clear();   // a freshly created server project has no custom colour yet
    remoteVersion_ = version;
    refreshActions();
    notify_->success(QString("Created \"%1\" on %2").arg(name, serverUrl));
  }

  // Save a server-linked session back: version-guarded name/layout PUT, then upload
  // the rendered result. A 409 surfaces a clear "edited elsewhere" message and
  // leaves the link untouched. Mirrors the browser's saveToServer/saveRemoteProject.
  void MainWindow::saveToServer() {
    if (!settings_.syncToServer) return;  // sync off — fetched project stays edit-in-memory only
    stencil::net::ServerClient* c =
        connections_ ? connections_->find(remoteAddress_) : nullptr;
    if (!c) {
      notify_->error(
          QString("Not connected to %1 — reconnect it first").arg(remoteAddress_));
      return;
    }
    // Guard the poll for the push duration (its nested REST event loop can pump the
    // poll timer) so we don't reload our own in-flight change.
    ScopedFlag pushGuard{remotePushing_};
    const int w = canvas_->imageWidth();
    const int h = canvas_->imageHeight();
    // Concurrent co-edit: on a version-guard conflict, merge the server's latest lines
    // with ours and retry — looping so a tight race (incl. the result upload's extra
    // version bump) still converges with both editors' annotations intact.
    qint64 newVersion = remoteVersion_;
    bool committed = false;
    bool merged = false;
    for (int attempt = 0; attempt < 6 && !committed; ++attempt) {
      const QJsonObject layout =
          fileStore::buildLayoutJson(w, h, canvas_->allLines(),
                                     settings_.imageFilter, settings_.filterColor,
                                     canvas_->cropRect(), canvas_->rotationQuarters(),
                                     currentLayoutMeta());
      bool conflict = false;
      if (c->updateProject(remoteId_, remoteName_, layout, remoteVersion_, newVersion,
                           conflict)) {
        remoteVersion_ = newVersion;
        committed = true;
        break;
      }
      if (!conflict) {
        notify_->error(QString("Server save failed — %1").arg(c->lastError()));
        return;
      }
      // Pull the peer's latest, union-merge their lines into ours (deduped), adopt the
      // server version, and loop to retry.
      stencil::net::ServerProject meta;
      QJsonObject srvLayout;
      if (!c->getProject(remoteId_, meta, srvLayout)) break;  // give up below
      int sw = 0, sh = 0;
      core::Lines mlines = fileStore::parseLayoutJson(srvLayout, sw, sh);
      QSet<QString> seen;
      for (const auto& l : mlines) seen.insert(lineKey(l));
      for (const auto& l : canvas_->allLines()) {
        const QString k = lineKey(l);
        if (!seen.contains(k)) { mlines.push_back(l); seen.insert(k); }
      }
      {  // apply merged lines (+ peer filter) locally without re-triggering a push
        ScopedFlag g{remoteReloading_};
        canvas_->setLines(mlines);
        // Adopt the peer's filter UNLESS this user changed their own, so a line-only
        // edit doesn't clobber the peer's filter change (the scalar can't merge).
        if (!filterDirty_) {
          QString sf, st;
          parseLayoutFilter(srvLayout, settings_.filterColor, sf, st);
          applyTintColor(QColor(st));
          applyImageFilter(sf);
        }
      }
      remoteVersion_ = meta.version;
      merged = true;
    }
    if (!committed) {
      notify_->error(
          "This project was edited elsewhere — reload it from the server before "
          "saving again");
      return;
    }
    filterDirty_ = false;   // our filter (if any) is now the server's
    // Upload the annotated render as the 'result'. The file write bumps the
    // version, so re-read it to keep the guard accurate for the next save.
    if (canvas_->hasImage()) {
      const QByteArray bytes = pngBytes(canvas_->renderToImage(true));
      if (c->uploadFile(remoteId_, "result", bytes, "png", w, h)) {
        stencil::net::ServerProject meta;
        QJsonObject lay;
        if (c->getProject(remoteId_, meta, lay)) remoteVersion_ = meta.version;
      }
    }
    // Don't announce another client's incoming change; just confirm our own save. (`merged`
    // still drives the union-merge retry loop above; it only affected this message.)
    (void)merged;
    notify_->success(QString("Saved \"%1\" to %2").arg(remoteName_, remoteAddress_));
  }

  void MainWindow::saveToActiveProject() {
    if (incognito_) {  // S6: no local save while incognito…
      const QStringList servers = connections_ ? connections_->urls() : QStringList();
      if (servers.isEmpty() || !canvas_->hasImage()) {
        notify_->info("Incognito mode — saving is disabled");
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
    if (!remoteAddress_.isEmpty()) {  // server-linked session → write back to the server
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
    // Keep provenance unless the active image carries its own (a save shouldn't
    // wipe links set via the Links dialog, but a fresh URL-loaded image updates them).
    if (!currentSource_.isEmpty()) pr->meta.source = currentSource_.toStdString();
    if (!currentResource_.isEmpty()) pr->meta.resource = currentResource_.toStdString();
    fileStore::saveProjects(projectList_);
    refreshDockMenu();  // bump it to the top of the Dock "recent" list
    notify_->success(
        QString("Saved to \"%1\"").arg(QString::fromStdString(pr->meta.name)));
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
    notify_->success(QString("Renamed to \"%1\"").arg(name));
    return true;
  }

  void MainWindow::updateProjectTitle() {
    QString name;
    bool editable = false;
    const bool remote = !remoteId_.isEmpty();
    if (incognito_) {
      name = "Incognito";
    } else if (!activeProjectId_.isEmpty()) {
      name = activeProjectName();
      editable = !name.isEmpty();
    } else if (remote) {
      name = remoteName_;   // server-linked session (no local project id)
      editable = !name.isEmpty();   // server projects are renameable too (pushed via commitProjectName)
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
    if (projectNameEditAction_) projectNameEditAction_->setVisible(editable && !nameEditing_);
    if (projectColorBtnAction_) projectColorBtnAction_->setVisible(editable && !nameEditing_);
    if (!nameEditing_) return;
    const QString v = projectName_->text().trimmed();
    // Compare against the CURRENT name — remoteName_ for a server-linked session (no local id),
    // else the local name.
    const QString current = !remoteId_.isEmpty() ? remoteName_ : activeProjectName();
    const bool changed = v != current;
    bool ok = changed;
    QString reason = changed ? QStringLiteral("Save name (Enter)") : QStringLiteral("No change");
    if (changed && remoteId_.isEmpty()) {
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
    if (!remoteId_.isEmpty()) {
      stencil::net::ServerClient* c = connections_ ? connections_->find(remoteAddress_) : nullptr;
      if (!newName.isEmpty() && newName != remoteName_ && c) {
        stencil::net::ServerProject meta;
        QJsonObject lay;
        qint64 newVersion = 0;
        bool conflict = false;
        if (c->getProject(remoteId_, meta, lay) &&
            c->updateProjectName(remoteId_, newName, meta.version, newVersion, conflict)) {
          remoteName_ = newName;
          remoteVersion_ = newVersion;
          notify_->success(QString("Renamed to \"%1\"").arg(newName));
        } else {
          notify_->error(QString("Rename failed: %1").arg(c ? c->lastError() : QStringLiteral("not connected")));
        }
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

  bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (obj == projectName_) {
      const QEvent::Type t = event->type();
      if (t == QEvent::MouseButtonDblClick) {
        // Double-click a read-only name → enter edit mode (browser parity).
        if (!nameEditing_) {
          enterNameEdit();
          return true;
        }
      } else if (t == QEvent::KeyPress) {
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
          // Escape always drops focus (clears the outline). If mid-edit, revert too.
          if (nameEditing_) cancelProjectName();
          else projectName_->clearFocus();
          return true;
        }
      } else if (t == QEvent::FocusOut) {
        // Clicking away leaves the edit: revert. Deferred so a click on ✓ commits first
        // (after which the field no longer has focus AND nameEditing_ is already false → no-op).
        if (nameEditing_) {
          QTimer::singleShot(0, this, [this] {
            if (nameEditing_ && projectName_ && !projectName_->hasFocus()) cancelProjectName();
          });
        }
      }
    }
    return QMainWindow::eventFilter(obj, event);
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
    return !remoteId_.isEmpty() ? remoteColor_ : activeProjectColor();
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
    const QColor seed = (!cur.isEmpty() && QColor(cur).isValid())
                            ? QColor(cur)
                            : accentPrimary(settings_.accentColor);
    // DontUseNativeDialog: the macOS native NSColorPanel is a shared floating panel that the
    // app-wide event filter / focus changes dismiss on mouse-move — Qt's own modal dialog runs a
    // self-contained nested loop and stays put. (Same reason native pickers misbehave here.)
    const QColor picked =
        QColorDialog::getColor(seed, this, "Project name colour", QColorDialog::DontUseNativeDialog);
    if (!picked.isValid()) return;   // user cancelled
    setActiveProjectColor(picked.name());
  }

  // Browser-style 🎨 popup: a tiny menu rather than opening the picker directly. Always offers
  // "Choose colour…"; offers "Use theme default colour" only when a custom colour is currently set.
  void MainWindow::showProjectColorMenu() {
    const QString cur = currentProjectColor();
    const bool hasCustom = !cur.isEmpty();
    QMenu menu(this);
    QAction* pick = menu.addAction("Choose colour…");
    // "Use theme default colour" is only meaningful when a custom colour is set — hide it
    // entirely (not just disable) when the project is already on the theme default.
    QAction* def = hasCustom ? menu.addAction("Use theme default colour") : nullptr;
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
    const QString fg =
        (!color.isEmpty() && c.isValid()) ? c.name() : QStringLiteral("#80868f");
    if (editing) {
      const QColor accent = accentPrimary(settings_.accentColor);
      projectName_->setStyleSheet(
          QString("QLineEdit{color:%1;border:1px solid %2;border-radius:6px;"
                  "background:palette(base);padding:2px 6px;}"
                  "QLineEdit:focus{border:1px solid %2;}")
              .arg(fg, accent.name()));
    } else {
      projectName_->setStyleSheet(
          QString("QLineEdit{color:%1;border:1px solid transparent;background:transparent;}"
                  "QLineEdit:focus{border:1px solid transparent;}")
              .arg(fg));
    }
  }

  void MainWindow::setActiveProjectColor(const QString& color) {
    const auto norm = normalizeProjectColor(color);
    if (!norm) {
      notify_->error("Invalid colour");
      return;
    }
    // A server-linked session has no local id: push the colour straight to the server.
    if (!remoteId_.isEmpty()) {
      if (setProjectColorById(remoteId_, remoteAddress_, *norm)) {
        remoteColor_ = *norm;
        updateProjectTitle();
      }
      return;
    }
    if (activeProjectId_.isEmpty()) {
      notify_->info("Open or save a project first");
      return;
    }
    if (setProjectColorById(activeProjectId_, QString(), *norm)) updateProjectTitle();
  }

  bool MainWindow::setProjectColorById(const QString& id, const QString& serverUrl,
                                       const QString& color) {
    const auto norm = normalizeProjectColor(color);
    if (!norm) {
      notify_->error("Invalid colour");
      return false;
    }
    // Server project: version-guarded PUT UpdateProject{color}. Refresh our linked
    // version when it's the open session so a later save doesn't 409.
    if (!serverUrl.isEmpty()) {
      stencil::net::ServerClient* c = connections_ ? connections_->find(serverUrl) : nullptr;
      if (!c) {
        notify_->error("Not connected to that server");
        return false;
      }
      stencil::net::ServerProject meta;
      QJsonObject lay;
      if (!c->getProject(id, meta, lay)) {
        notify_->error(QString("Colour update failed: %1").arg(c->lastError()));
        return false;
      }
      qint64 newVersion = 0;
      bool conflict = false;
      if (!c->updateProjectColor(id, *norm, meta.version, newVersion, conflict)) {
        notify_->error(QString("Colour update failed: %1").arg(c->lastError()));
        return false;
      }
      if (remoteId_ == id && remoteAddress_ == serverUrl) remoteVersion_ = newVersion;
      notify_->success(norm->isEmpty() ? QStringLiteral("Colour reset to theme default")
                                       : QString("Colour set to %1").arg(*norm));
      return true;
    }
    // Local project: update the meta + persist.
    Project* pr = findProject(id.toStdString());
    if (!pr) return false;
    pr->meta.color = norm->toStdString();
    fileStore::saveProjects(projectList_);
    refreshDockMenu();
    notify_->success(norm->isEmpty() ? QStringLiteral("Colour reset to theme default")
                                     : QString("Colour set to %1").arg(*norm));
    return true;
  }

  void MainWindow::openInfo() {
    InfoDialog dlg(this);
    dlg.exec();
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
    if (dlg.exec() != QDialog::Accepted) return;

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
    notify_->success("Shortcuts updated");
  }

  void MainWindow::updateStatusIdle() {
    status_->setText(canvas_->hasImage()
                         ? "Ready"
                         : "Open an image — or create a blank one — to begin");
  }

}
