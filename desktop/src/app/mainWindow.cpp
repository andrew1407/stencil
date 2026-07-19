#include "mainWindow.hpp"
#include "expirationDialog.hpp"
#include "deepLink.hpp"
#include "openImageDialog.hpp"
#include "openInDialog.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "dropZonesOverlay.hpp"
#include "incognitoOverlay.hpp"
#include "projectDragZones.hpp"
#include "cropGeometry.hpp"
#include "geometry.hpp"
#include "pageMetrics.hpp"
#include "cropDialog.hpp"
#include "tooltipRows.hpp"
#include "zoomPan.hpp"
#include "guiHelpers.hpp"
#include "searchCombo.hpp"
#include "iconSet.hpp"
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
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "theme.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QBuffer>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QEasingCurve>
#include <QEventLoop>
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
#include <QLineEdit>
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
#include <QTimer>
#include <QToolBar>
#include <QStyle>
#include <QToolButton>
#include <QWidgetAction>
#include <algorithm>

namespace stencil::gui {

  namespace {
    // Forward-declared here (defined lower in this TU's anon namespace) so the constructor's
    // ProjectTransferController fetchUrlBytes hook + openServerProject can name it.
    void fetchUrlBytesAsync(QObject* ctx, const QString& url, std::function<void(QByteArray)> done);

    long long nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

    // A QMenu whose hosted checkbox/radio rows toggle WITHOUT closing the menu (browser parity:
    // the inline controls stay live). QMenu's own mouseReleaseEvent closes the popup on release
    // over a QWidgetAction, so we intercept clicks that land on a hosted button: drive the button
    // and swallow the event, never calling the base handler. Normal action rows fall through.
    class StayOpenMenu : public QMenu {
     public:
      using QMenu::QMenu;

     protected:
      QAbstractButton* toggleAt(const QPoint& p) {
        for (QWidget* c = childAt(p); c && c != this; c = c->parentWidget()) {
          if (auto* b = qobject_cast<QAbstractButton*>(c)) return b;
          if (auto* b = c->findChild<QAbstractButton*>()) return b;
        }
        return nullptr;
      }
      // A checkable, enabled plain action under the cursor (e.g. Show Points / Show Lines, or the
      // Style line-style radios) that should toggle in place instead of dismissing the menu.
      QAction* checkableAt(const QPoint& p) {
        QAction* a = actionAt(p);
        return (a && a->isCheckable() && a->isEnabled()) ? a : nullptr;
      }
      void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && (toggleAt(e->pos()) || checkableAt(e->pos()))) {
          e->accept();
          return;
        }
        QMenu::mousePressEvent(e);
      }
      void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
          if (QAbstractButton* b = toggleAt(e->pos())) {
            if (b->isEnabled()) b->click();   // checkbox toggles; radio checks (exclusive group)
            e->accept();
            return;                           // do NOT call base → the menu stays open
          }
          if (QAction* a = checkableAt(e->pos())) {
            // Exclusive group → select (never uncheck); independent toggle → flip. Fires toggled.
            if (QActionGroup* g = a->actionGroup(); g && g->isExclusive())
              a->setChecked(true);
            else
              a->toggle();
            e->accept();
            return;                           // keep the menu open
          }
        }
        QMenu::mouseReleaseEvent(e);
      }
    };

    // ── Hover "glass shimmer": a left→right light sweep played on hover — the desktop match for
    // the browser/extension CSS shimmer. Qt style sheets can't animate a sweep, so this is a
    // transparent, mouse-through child overlay that paints an animated diagonal highlight.
    // installHoverShimmer(w) attaches one to any button; it lives/dies with its target.
    class ShimmerOverlay : public QWidget {
    public:
      // Whole-widget mode: sweeps the whole target on hover-enter. View mode (view != null): sweeps
      // the hovered ROW of an item view (points/lines panel), tracked via the viewport's mouse-move.
      explicit ShimmerOverlay(QWidget* target, QAbstractItemView* view = nullptr)
          : QWidget(view ? view->viewport() : target),
            target_(view ? view->viewport() : target), view_(view) {
        // A child overlay that alpha-blends over the target. NO WA_TranslucentBackground (that's a
        // top-level-window attribute and stops a child from rendering); WA_NoSystemBackground so
        // Qt doesn't erase our area and the target shows through the un-painted (transparent) parts.
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        anim_ = new QVariantAnimation(this);
        anim_->setStartValue(0.0);
        anim_->setEndValue(1.0);
        anim_->setDuration(650);
        anim_->setEasingCurve(QEasingCurve::InOutSine);
        QObject::connect(anim_, &QVariantAnimation::valueChanged, this,
                         [this](const QVariant& v) { progress_ = v.toReal(); update(); });
        QObject::connect(anim_, &QVariantAnimation::finished, this, [this] { progress_ = -1.0; update(); });
        target_->installEventFilter(this);
        if (view_) target_->setMouseTracking(true);   // so we get MouseMove without a button held
        setGeometry(target_->rect());
        raise();
        show();   // stays present (transparent); paints only while the sweep animates
      }

    protected:
      bool eventFilter(QObject* o, QEvent* e) override {
        if (o == target_) {
          switch (e->type()) {
            case QEvent::Resize:
            case QEvent::Move:
            case QEvent::Show:
              setGeometry(target_->rect());
              raise();
              break;
            case QEvent::Enter:
              if (!view_ && target_->isEnabled()) startSweep(rect());
              break;
            case QEvent::Leave:
              // Cancel the sweep the instant the cursor leaves, so a fast pass over many items
              // doesn't leave a trail of animations still playing out on already-unhovered widgets.
              hoveredRow_ = -1;
              anim_->stop();
              progress_ = -1.0;
              update();
              break;
            case QEvent::MouseMove:
              if (view_) {
                const QModelIndex idx = view_->indexAt(static_cast<QMouseEvent*>(e)->pos());
                const int row = idx.isValid() ? idx.row() : -1;
                if (row != hoveredRow_) {
                  hoveredRow_ = row;
                  if (row >= 0) {
                    QRect r = view_->visualRect(idx);
                    r.setLeft(0);
                    r.setRight(target_->width());
                    startSweep(r);
                  }
                }
              }
              break;
            default:
              break;
          }
        }
        return QWidget::eventFilter(o, e);
      }
      void paintEvent(QPaintEvent*) override {
        if (progress_ < 0.0) return;
        const QRect b = band_.isEmpty() ? rect() : band_;
        if (b.width() <= 0 || b.height() <= 0) return;
        const qreal bw = b.width() * 0.5;
        const qreal cx = b.left() - bw + progress_ * (b.width() + 2 * bw);   // off-left → off-right
        QLinearGradient g(cx - bw, b.top(), cx + bw, b.bottom());            // diagonal light band
        g.setColorAt(0.0, QColor(255, 255, 255, 0));
        g.setColorAt(0.5, QColor(255, 255, 255, 95));
        g.setColorAt(1.0, QColor(255, 255, 255, 0));
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(b, g);
      }

    private:
      void startSweep(const QRect& band) { band_ = band; anim_->stop(); anim_->start(); }
      QWidget* target_;
      QAbstractItemView* view_;
      QVariantAnimation* anim_ = nullptr;
      qreal progress_ = -1.0;
      QRect band_;
      int hoveredRow_ = -1;
    };

    void installHoverShimmer(QWidget* target) {
      if (target && !target->property("_shimmer").toBool()) {
        target->setProperty("_shimmer", true);   // guard against double-install
        new ShimmerOverlay(target);               // parented to target
      }
    }
    void installRowShimmer(QAbstractItemView* view) {
      if (view && !view->property("_shimmer").toBool()) {
        view->setProperty("_shimmer", true);
        new ShimmerOverlay(nullptr, view);   // parented to the view's viewport
      }
    }

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
    // The canvas is sized to the image, so a zoomed-out image leaves margin around it
    // that belongs to the viewport, not the canvas. Filter the viewport so Ctrl+wheel /
    // trackpad pinch there still zoom (otherwise you can't zoom a small image back up).
    scroll_->viewport()->installEventFilter(this);
    // App-wide filter so Escape can leave fullscreen from any focus (see eventFilter).
    qApp->installEventFilter(this);

    selPanel_ = new SelectionPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, selPanel_);
    // Shared hover shimmer for the right Points/Lines panel: per-button on its buttons, and
    // per-ROW on its points table + lines list (item-view rows aren't widgets, so the overlay
    // tracks the hovered row) — matching the browser's coord-panel shimmer.
    for (QAbstractButton* b : selPanel_->findChildren<QAbstractButton*>()) installHoverShimmer(b);
    for (QAbstractItemView* v : selPanel_->findChildren<QAbstractItemView*>()) installRowShimmer(v);

    notify_ = new Notifications(scroll_->viewport());
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
    dropZones_->setAccent(palette().color(QPalette::Highlight));
    // 3-zone overlay shown behind the Projects dialog while a project row is dragged out of it.
    projectZones_ = new ProjectDragZones(scroll_->viewport());
    tooltip_ = new CanvasTooltip(this);  // floating hover tooltip (S12)

    // Live cursor coord readout (Pixel/Page/To edge) at the bottom of the window — the desktop
    // equivalent of the browser's #coord-status bar below the canvas. It's empty while the cursor
    // is off the canvas (no "Ready" filler, matching the browser) and hidden during fullscreen.
    status_ = new QLabel("Open an image — or create a blank one — to begin", this);
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
    pageSize_->setToolTip(
        "Page format used for cm/inch measurements (ISO A/B/C series, or custom)");
    zoom_ = new QComboBox(this);
    zoom_->addItems({"10%", "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%", "500%", "800%", "1600%", "3200%"});
    zoom_->setToolTip("Zoom level — pick a preset or type an exact percent");
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

  // Defined here (not =default in the header) so unique_ptr members of forward-declared
  // types are destroyed where their complete type is visible (dataExportController.hpp above).
  MainWindow::~MainWindow() = default;

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
      remoteSync_->scheduleRemotePush();  // formulas ride the layout — push to peers
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

    // The single Open entry: the unified Open dialog (local file, URL, or a new blank
    // canvas). It replaces the former split of Open Image / Open Another Image / New
    // Blank Image, mirroring the browser's one "Open Image" button.
    actOpen_ = mk("Open Image…", hotkey("loadImage", "Ctrl+O"));
    tip(actOpen_,
        "Open an image — a local file, a web URL, or a new blank canvas");
    // Emoji prefixes were removed from these labels now that every action carries
    // a themed line-art icon (styleActionIcons): the menu shows icon + clean text,
    // the icon-only toolbar shows the glyph with the label on its tooltip.
    // New Line has no entry in the shared hotkeysConfig.json registry, so its
    // (browser-coordinated) default is set literally rather than through hotkey().
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
    tip(actCycleFilter_,
        "Cycle the image filter (none → B&W → sepia → invert → contour → tint)");
    // Compare view: cycle none → original → vertical split → horizontal split. The
    // toolbar combo + View submenu offer direct picks; hold Alt+Shift+O to peek.
    actCycleCompare_ = mk("Cycle Compare View", hotkey("cycleCompare", "Alt+O"));
    tip(actCycleCompare_,
        "Cycle the compare view (none → original → vertical split → horizontal split); "
        "hold Alt+Shift+O to peek at the original");
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
    actToolbars_ = mk("Toolbars", hotkey("toggleControls", "Alt+C"));  // show/hide the top toolbars
    actFullscreen_ = mk("Fullscreen", hotkey("fullscreen", "Alt+F"));
    actSettings_ = mk("Settings…", "Ctrl+,");
    actProjects_ = mk("Projects…", hotkey("openProjects", "Ctrl+Shift+P"));
    actConnect_ = mk("Servers…", hotkey("openServers", "Ctrl+Shift+K"));
    tip(actConnect_,
        "Connect to collaboration servers — shared projects appear with a golden outline");
    actLinks_ = mk("Image Links…", hotkey("openLinks", "Ctrl+Shift+L"));
    actOpenIn_ = mk("Open In…", hotkey("openIn", "Ctrl+Shift+E"));
    tip(actOpenIn_,
        "Open the current project in the browser app or the Telegram bot");
    // New Project has no shared hotkeysConfig.json entry; literal default here.
    actNewProject_ = mk("New Project", "Ctrl+Shift+N");
    actSaveProject_ = mk("Save to Project", "Ctrl+Shift+S");
    // Trash: clear (remove) the current project/editor (mirrors the browser's
    // #clear-storage danger button). Hidden for server projects (refreshActions).
    actClearProject_ = mk("Clear Project", QString());
    tip(actClearProject_, "Clear (remove) current project");
    actSaveSession_ = mk("Save Session", "Ctrl+S");
    actInfo_ = mk("Info && Shortcuts", hotkey("openHelp", "F1"));
    actIncognito_ = mk("Incognito", hotkey("toggleIncognito", "Alt+I"));
    actTooltip_ = mk("Show Tooltips", QString());   // browser label parity (was "Hover Tooltip")
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
    // Whole-project files (.stencil): image + layout + settings + theme in one portable file.
    actSaveProjectFile_ = mk("Save Project As… (.stencil)", hotkey("saveProject", "Ctrl+Shift+S"));
    actOpenProjectFile_ = mk("Open Project… (.stencil)", hotkey("openProject", "Ctrl+Shift+F"));
    actStencilLiveSync_ = mk("Live Sync with File", QString());
    actStencilLiveSync_->setCheckable(true);
    actStencilLiveSync_->setEnabled(false);   // enabled once the project is linked to a .stencil file
    actStencilLiveSync_->setToolTip("Auto-save edits to the linked .stencil file and reload it when another app changes it");
    actCopyLayout_ = mk("Copy Layout JSON", hotkey("copyLayout", "Ctrl+Alt+C"));
    actPasteLayout_ = mk("Paste Layout JSON", QString());
    actSaveImage_ = mk("Save Image…", hotkey("saveImage", "Ctrl+Shift+D"));
    actCopyImage_ = mk("Copy Image to Clipboard", hotkey("copyImage", "Ctrl+C"));
    // Single Ctrl+V entrypoint (paste hotkey): image takes priority over a layout
    // JSON text payload, mirroring the browser paste listener (drawingApp.js
    // :563-591). pasteImage() does that dispatch.
    actPasteImage_ = mk("Paste (Image or Layout)", hotkey("paste", "Ctrl+V"));

    // Layout/image export + clipboard actions route to DataExportController (dataExport_).
    connect(actDownloadJson_, &QAction::triggered, this, [this] { dataExport_->downloadLayout(); });
    connect(actUploadJson_, &QAction::triggered, this, [this] { dataExport_->uploadLayout(); });
    connect(actSaveProjectFile_, &QAction::triggered, this, [this] { saveProjectFileAs(); });
    connect(actStencilLiveSync_, &QAction::toggled, this, [this](bool on) { toggleStencilLiveSync(on); });
    connect(actOpenProjectFile_, &QAction::triggered, this, [this] {
      const QString path = QFileDialog::getOpenFileName(
          this, "Open project", QString(), "Stencil project (*.stencil)");
      if (!path.isEmpty()) openProjectFile(path);
    });
    connect(actCopyLayout_, &QAction::triggered, this, [this] { dataExport_->copyLayout(); });
    connect(actPasteLayout_, &QAction::triggered, this, [this] { dataExport_->pasteLayout(); });
    connect(actSaveImage_, &QAction::triggered, this, [this] { dataExport_->saveImageFile(); });
    connect(actCopyImage_, &QAction::triggered, this, [this] { dataExport_->copyImageToClipboard(); });
    connect(actPasteImage_, &QAction::triggered, this, &MainWindow::pasteImage);

    // Incognito (S6): edit without saving. Togglable only before an image is
    // loaded (browser behavior), so it gets disabled once content exists.
    actIncognito_->setCheckable(true);
    actIncognito_->setToolTip(
        "Incognito — edit without saving (choose before adding an image)");

    // Fullscreen is a toggle: its toolbar button shows the accent "active" fill
    // (QToolButton:checked) while fullscreen is on, mirroring the browser.
    actFullscreen_->setCheckable(true);
    actShowPoints_->setCheckable(true);
    actShowLines_->setCheckable(true);
    actPanel_->setCheckable(true);
    actPanel_->setChecked(true);
    actToolbars_->setCheckable(true);
    actToolbars_->setChecked(true);

    connect(actOpen_, &QAction::triggered, this, &MainWindow::openImage);
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
    // Alt+R is a global shortcut, so it fires (and consumes the key) before keyPressEvent —
    // with a line selected we arm the line-rotate chord here instead of rotating the image, so
    // the following ←/→ rotates the selection (keyPressEvent). Deselect to rotate the image.
    connect(actRotateLeft_, &QAction::triggered, this, [this, rotate] {
      if (canvas_ && canvas_->selectionCount() >= 1) { rKeyHeld_ = true; return; }
      rotate(false);
    });
    connect(actRotateRight_, &QAction::triggered, this, [rotate] { rotate(true); });
    // Cycle the image filter (Alt+B) — mirrors the browser's cycleFilter hotkey:
    // none → bw → sepia → invert → contour → custom(tint). applyImageFilter
    // marks it dirty + syncs.
    connect(actCycleFilter_, &QAction::triggered, this, [this] {
      if (!canvas_->hasImage()) return;
      static const QStringList order{"none",   "bw",      "sepia",
                                     "invert", "contour", "custom"};
      const int cur = order.indexOf(settings_.imageFilter);
      applyImageFilter(order[(cur + 1) % order.size()]);
    });
    // Cycle the compare view (Alt+O): none → original → vertical → horizontal.
    connect(actCycleCompare_, &QAction::triggered, this, [this] {
      if (!canvas_->hasImage()) return;
      static const QStringList order{"none", "original", "vertical", "horizontal"};
      const int cur = order.indexOf(canvas_->compareMode());
      setCompareModeUi(order[(cur + 1) % order.size()]);
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
    // Points panel + top-menu (toolbars) show/hide, animated (slide). The floating arrow overlays
    // and the View-menu/hotkey both route through these actions.
    connect(actPanel_, &QAction::toggled, this, [this](bool on) { setPanelShown(on, true); });
    connect(actToolbars_, &QAction::toggled, this, [this](bool on) { setToolbarsShown(on, true); });
    connect(actFullscreen_, &QAction::triggered, this,
            &MainWindow::toggleFullscreen);
    // Escape-leaves-fullscreen is handled in the app-wide eventFilter (reliable across focus).
    fsHoverTimer_ = new QTimer(this);   // drives the fullscreen edge-hover reveal
    connect(fsHoverTimer_, &QTimer::timeout, this, &MainWindow::fsHoverTick);
    connect(actSettings_, &QAction::triggered, this, &MainWindow::openSettings);
    connect(actProjects_, &QAction::triggered, this, &MainWindow::openProjects);
    connect(actConnect_, &QAction::triggered, this, &MainWindow::openConnections);
    connect(actLinks_, &QAction::triggered, this, &MainWindow::openLinks);
    connect(actOpenIn_, &QAction::triggered, this, &MainWindow::openInAnotherApp);
    connect(actNewProject_, &QAction::triggered, this,
            &MainWindow::newProjectFromCanvas);
    connect(actSaveProject_, &QAction::triggered, this,
            &MainWindow::saveToActiveProject);
    connect(actClearProject_, &QAction::triggered, this,
            &MainWindow::clearCurrentProject);
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
    hotkeyActions_["toggleControls"] = actToolbars_;
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
    hotkeyActions_["cropImage"] = actCrop_;
    hotkeyActions_["saveImage"] = actSaveImage_;
    hotkeyActions_["downloadJson"] = actDownloadJson_;
    hotkeyActions_["uploadJson"] = actUploadJson_;
    hotkeyActions_["saveProject"] = actSaveProjectFile_;
    hotkeyActions_["openProject"] = actOpenProjectFile_;
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
    // Shared QWidgetAction row scaffold: a QWidget + HBox with the standard menu-row margins,
    // wrapped in a QWidgetAction (so clicking the hosted control doesn't dismiss the menu).
    // Returns the layout to fill; its parentWidget() is the host QWidget. Sets `act`.
    auto makeMenuRow = [this](QWidgetAction*& act, int topM = 4, int botM = 4) {
      auto* w = new QWidget(this);
      auto* lay = new QHBoxLayout(w);
      lay->setContentsMargins(14, topM, 14, botM);
      act = new QWidgetAction(this);
      act->setDefaultWidget(w);
      return lay;
    };
    auto styleRow = [this, &makeMenuRow](const QString& label, QSpinBox*& spin, int lo, int hi,
                                         QWidgetAction*& act) {
      auto* lay = makeMenuRow(act);
      auto* w = lay->parentWidget();
      lay->addWidget(new QLabel(label, w));
      spin = new QSpinBox(w);
      spin->setRange(lo, hi);
      lay->addStretch(1);
      lay->addWidget(spin);
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
    filterButtons_ = new QButtonGroup(this);
    filterButtons_->setExclusive(true);
    auto mkFilter = [this, &makeMenuRow](const QString& text, const QString& value) {
      QWidgetAction* act;
      auto* lay = makeMenuRow(act);
      auto* rb = new QRadioButton(text, lay->parentWidget());
      rb->setProperty("filterValue", value);
      filterButtons_->addButton(rb);
      // Expand across the row so the whole strip is the radio's hit area (label + trailing space),
      // and the radio itself consumes the click so the menu stays open.
      rb->setSizePolicy(QSizePolicy::Expanding, rb->sizePolicy().verticalPolicy());
      lay->addWidget(rb);
      // toggled(true) fires for the newly-selected radio; applyImageFilter is a no-op-safe re-set.
      connect(rb, &QRadioButton::toggled, this,
              [this, value](bool on) { if (on) applyImageFilter(value); });
      return act;
    };
    actFilterNone_ = mkFilter("None", "none");
    actFilterBW_ = mkFilter("Black && White", "bw");
    actFilterSepia_ = mkFilter("Sepia", "sepia");
    actFilterInvert_ = mkFilter("Invert", "invert");
    actFilterContour_ = mkFilter("Contour", "contour");
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

    // ── Tooltip toggles (contextMenu.js:96-107, 546-557). Hosted as real QCheckBoxes
    // in QWidgetActions (like the marker/thickness spinbox rows) so a click flips them
    // WITHOUT dismissing the menu — the browser's context menu likewise keeps its inline
    // checkboxes/sliders live — and so they render as checkboxes, not the action's icon.
    // Per-row visibility is backed by the MainWindow booleans (consumed in onHoverDetail).
    auto mkCheckRow = [this, &makeMenuRow](const QString& text, bool checked, QCheckBox*& box,
                                           QWidgetAction*& act) {
      auto* lay = makeMenuRow(act);
      box = new QCheckBox(text, lay->parentWidget());
      box->setChecked(checked);
      // Expand the button across the row so its own hit area (which toggles AND consumes the
      // click, keeping the menu open) covers the whole strip — label and trailing space included,
      // not just the tiny indicator. QMenu widens the QWidgetAction widget to the menu width.
      box->setSizePolicy(QSizePolicy::Expanding, box->sizePolicy().verticalPolicy());
      lay->addWidget(box);
    };
    // Enable toggle: drives settings_.tooltipEnabled and mirrors the View-menu actTooltip_.
    mkCheckRow("Show Tooltips", settings_.tooltipEnabled, tooltipEnableCheck_, actTooltipEnable_);
    connect(tooltipEnableCheck_, &QCheckBox::toggled, this, [this](bool on) {
      settings_.tooltipEnabled = on;
      {
        QSignalBlocker b(actTooltip_);
        actTooltip_->setChecked(on);  // keep the View-menu item in lock-step
      }
      persistSettings();
      if (!on)
        tooltip_->hide();
      else if (!QApplication::activePopupWidget())
        onHovered(lastHoverX_, lastHoverY_);  // re-show at the current hover (not while the menu's up)
    });
    // The three per-row toggles write straight into settings_ (the source of truth), persist,
    // and refresh the live tooltip. Binding `backing` to the settings_ field keeps them in sync.
    auto mkRowToggle = [this, &mkCheckRow](const QString& text, bool& backing,
                                           QCheckBox*& box, QWidgetAction*& act) {
      mkCheckRow(text, backing, box, act);
      connect(box, &QCheckBox::toggled, this, [this, &backing](bool on) {
        backing = on;
        persistSettings();
        // Don't refresh the live tooltip while the context menu is up — showing that top-level
        // tooltip window would steal the popup's grab and dismiss the menu.
        if (!QApplication::activePopupWidget()) onHovered(lastHoverX_, lastHoverY_);
      });
    };
    mkRowToggle("Page (cm)", settings_.tooltipShowPage, ttPageCheck_, actTtPage_);
    mkRowToggle("Screen (px)", settings_.tooltipShowScreen, ttScreenCheck_, actTtScreen_);
    mkRowToggle("To Edge (cm)", settings_.tooltipShowCoords, ttCoordsCheck_, actTtCoords_);

    // ── Transformation submenu formula controls (contextMenu.js:84-100): an "Allow Formulas"
    // checkbox and x(x)/y(y) inputs, hosted so the submenu stays open. They are twins of the
    // toolbar formula widgets — edits here drive those (setChecked/setText), so the existing
    // validate/apply/persist/co-edit-push pipeline runs unchanged. Seeded in syncContextActions.
    mkCheckRow("Allow Formulas", settings_.allowFormulas, ctxAllowFormulas_, ctxAllowFormulasAct_);
    connect(ctxAllowFormulas_, &QCheckBox::toggled, this, [this](bool on) {
      allowFormulas_->setChecked(on);   // the canonical toolbar handler does settings/persist/apply
      if (ctxFormulaXAct_) ctxFormulaXAct_->setVisible(on);
      if (ctxFormulaYAct_) ctxFormulaYAct_->setVisible(on);
    });
    auto mkFormulaRow = [this, &makeMenuRow](const QString& label, const QString& placeholder,
                                             QLineEdit*& edit, QWidgetAction*& act) {
      auto* lay = makeMenuRow(act, 2, 4);
      auto* w = lay->parentWidget();
      lay->addWidget(new QLabel(label, w));
      edit = new QLineEdit(w);
      edit->setPlaceholderText(placeholder);
      edit->setFixedWidth(150);
      lay->addStretch(1);
      lay->addWidget(edit);
    };
    mkFormulaRow("x(x)=", "e.g. x + 9", ctxFormulaX_, ctxFormulaXAct_);
    mkFormulaRow("y(y)=", "e.g. (y-7)*4", ctxFormulaY_, ctxFormulaYAct_);
    // Mirror context edits into the canonical toolbar inputs (guarded to avoid a feedback loop),
    // which fires validateAndApplyFormulas() with its inline error + persistence.
    connect(ctxFormulaX_, &QLineEdit::textChanged, this, [this](const QString& t) {
      if (formulaX_->text() != t) formulaX_->setText(t);
    });
    connect(ctxFormulaY_, &QLineEdit::textChanged, this, [this](const QString& t) {
      if (formulaY_->text() != t) formulaY_->setText(t);
    });

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
    // On macOS, however, the native global menu bar at the top of the screen is
    // the expected placement, so leave Qt's default (native) there.
#ifndef Q_OS_MACOS
    menuBar()->setNativeMenuBar(false);
#endif
    // Mnemonics avoid the Alt+letter combos bound to hotkeys (Alt+F fullscreen,
    // Alt+P points, Alt+L lines, etc.).
    auto* file = menuBar()->addMenu("F&ile");
    file->addAction(actOpen_);
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
    data->addAction(actOpenProjectFile_);
    data->addAction(actSaveProjectFile_);
    data->addAction(actStencilLiveSync_);
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
    // Compare-with-original submenu: radio set kept in sync with the toolbar combo.
    auto* compareMenu = view->addMenu("&Compare");
    compareGroup_ = new QActionGroup(this);
    auto mkCompare = [&](const QString& text, const QString& value) {
      auto* a = compareMenu->addAction(text);
      a->setCheckable(true);
      a->setData(value);
      a->setChecked(value == "none");
      compareGroup_->addAction(a);
      connect(a, &QAction::triggered, this, [this, value] { setCompareModeUi(value); });
    };
    mkCompare("None", "none");
    mkCompare("Original only", "original");
    mkCompare("Vertical split (original | edit)", "vertical");
    mkCompare("Horizontal split (original / edit)", "horizontal");
    compareMenu->addSeparator();
    compareMenu->addAction(actCycleCompare_);
    view->addAction(actPanel_);
    view->addAction(actToolbars_);
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
    project->addAction(actClearProject_);
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
    project->addAction(actOpenIn_);

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
    buildImageInfoBar();
    // Shared hover shimmer on every interactive control across the toolbar rows (buttons, combos,
    // spinboxes, the f(x,y) checkbox, text fields) so the whole toolbar has one consistent hover
    // treatment — not just the makeToolSection icons.
    for (QToolBar* tb : findChildren<QToolBar*>()) {
      for (QToolButton* b : tb->findChildren<QToolButton*>())
        if (b != logoBtn_) installHoverShimmer(b);   // skip the logo (its own art/affordance)
      for (QComboBox* c : tb->findChildren<QComboBox*>()) installHoverShimmer(c);
      for (QAbstractSpinBox* s : tb->findChildren<QAbstractSpinBox*>()) installHoverShimmer(s);
      for (QCheckBox* c : tb->findChildren<QCheckBox*>()) installHoverShimmer(c);   // f(x,y) pill
      for (QLineEdit* le : tb->findChildren<QLineEdit*>())
        if (le != projectName_) installHoverShimmer(le);   // skip the rename field
    }
  }

  // Build a toolbar "section": a small uppercase label ABOVE a horizontal strip of the given
  // actions' buttons (+ optional trailing widgets like the zoom combo). The desktop counterpart
  // of the browser's stacked .ctrl-section (label on top of the button row), so the toolbar groups
  // are NAMED with the header above the icons — not a bare inline label beside them.
  QWidget* MainWindow::makeToolSection(const QString& title, const QList<QAction*>& actions,
                                       const QList<QWidget*>& extras) {
    auto* section = new QWidget(this);
    auto* col = new QVBoxLayout(section);
    col->setContentsMargins(6, 1, 6, 1);
    col->setSpacing(3);
    auto* label = new QLabel(title.toUpper(), section);
    label->setObjectName("sectionLabel");
    label->setStyleSheet("color:#7a828c;font-size:9px;font-weight:700;letter-spacing:0.6px;");
    label->setAlignment(Qt::AlignLeft);   // left-aligned header, matching the browser sections
    col->addWidget(label);
    auto* rowWidget = new QWidget(section);
    auto* row = new QHBoxLayout(rowWidget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    for (QAction* a : actions) {
      auto* btn = new QToolButton(rowWidget);
      btn->setDefaultAction(a);   // reflects the action's icon / tooltip / enabled / checked state
      btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
      btn->setAutoRaise(true);
      btn->setIconSize(QSize(18, 18));
      // A standalone QToolButton does NOT auto-hide when its action is hidden (unlike a toolbar
      // action-widget), so mirror visibility explicitly for the gated ones (Open-in, Clear-project).
      btn->setVisible(a->isVisible());
      connect(a, &QAction::changed, btn, [a, btn] { btn->setVisible(a->isVisible()); });
      if (a == actStartDraw_) startDrawBtn_ = btn;   // styled accent while a draw session is active
      row->addWidget(btn);
    }
    for (QWidget* ex : extras) { ex->setParent(rowWidget); row->addWidget(ex); }
    col->addWidget(rowWidget);
    return section;
  }

  void MainWindow::buildMainToolbar() {
    // ── Header row (always visible): the "Controls" collapse pill + the project-name group.
    // This row stays put while the tool rows below (Main / Page&Formula / Style) slide open/closed,
    // exactly like the browser's header that keeps the "⌃ Controls" pill + title when the body hides.
    headerToolbar_ = addToolBar("Header");
    headerToolbar_->setMovable(false);
    // App logo (mini line-chart, mirrors the browser's top-left logo). Clicking it cycles the theme
    // accent to the next preset — the same affordance as the browser's clickable logo.
    logoBtn_ = new QToolButton(this);
    logoBtn_->setCursor(Qt::PointingHandCursor);
    logoBtn_->setIconSize(QSize(24, 24));
    logoBtn_->setIcon(QIcon(makeLogoPixmap(24)));
    logoBtn_->setToolTip(QString());   // no tooltip on the logo
    // No hover highlight — flat, transparent, borderless (just the logo art).
    logoBtn_->setStyleSheet("QToolButton{border:none;background:transparent;padding:3px;}");
    // Single click cycles the accent, but DEFER it briefly so a double-click can pre-empt it and open
    // the custom-colour picker instead (mirrors the browser logo's click-vs-dblclick behaviour).
    logoClickTimer_ = new QTimer(this);
    logoClickTimer_->setSingleShot(true);
    connect(logoClickTimer_, &QTimer::timeout, this, [this] {
      const auto& presets = accentPresets();
      if (presets.empty()) return;
      int idx = -1;
      for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].key == settings_.accentColor) { idx = static_cast<int>(i); break; }
      auto next = settings_;
      // Browser parity: a CUSTOM colour (not a preset — idx < 0) resets to the default (violet);
      // otherwise advance to the next preset, wrapping.
      next.accentColor = idx < 0 ? presets.front().key : presets[(idx + 1) % presets.size()].key;
      applySettings(next, true);   // apply + persist (re-themes everything, incl. the logo frame)
    });
    connect(logoBtn_, &QToolButton::clicked, this, [this] { logoClickTimer_->start(250); });
    logoBtn_->installEventFilter(this);   // catch double-click → custom colour picker (see eventFilter)
    headerToolbar_->addWidget(logoBtn_);
    // "Controls" chevron pill — collapses/expands the tool rows (routes through actToolbars_ so the
    // View-menu entry + Alt+C hotkey stay in sync). Icon (chevron) themed in styleActionIcons.
    controlsPill_ = new QToolButton(this);
    controlsPill_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    controlsPill_->setText("Controls");
    controlsPill_->setAutoRaise(true);
    controlsPill_->setCursor(Qt::PointingHandCursor);
    controlsPill_->setToolTip(QString("Show / hide the toolbars (%1)").arg(hotkey("toggleControls", "Alt+C")));
    connect(controlsPill_, &QToolButton::clicked, this, [this] { if (actToolbars_) actToolbars_->toggle(); });
    headerToolbar_->addWidget(controlsPill_);
    headerToolbar_->addSeparator();
    buildProjectNameGroup(headerToolbar_);
    // "Image Size: W × H px" readout. Created here but placed in its own full-width bar
    // BELOW the toolbars (see buildImageInfoBar) — browser parity with the #image-info bar,
    // left-aligned above the canvas rather than tucked in the top-right corner.
    imageSizeInfo_ = new QLabel(this);
    imageSizeInfo_->setStyleSheet("color:#9aa0a8;padding:2px 10px;");
    addToolBarBreak();

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

    // Named, stacked groups (label ABOVE the icon row via makeToolSection) mirror the browser
    // topbar order: Image · Projects · Share · Edit · Draw · Zoom · Settings.
    // Blank-background swatch (browser parity): a colour button shown only for blank projects,
    // recolouring the fill (lines kept). Lives in the IMAGE group; gated in updateProjectTitle.
    blankColorBtn_ = new QToolButton(this);
    blankColorBtn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    blankColorBtn_->setAutoRaise(true);
    blankColorBtn_->setIconSize(QSize(18, 18));
    blankColorBtn_->setToolTip("Blank background colour — recolour this blank image (keeps your lines)");
    blankColorBtn_->setVisible(false);
    connect(blankColorBtn_, &QToolButton::clicked, this, [this] { setActiveBlankColor(); });

    // Image = open + the per-image actions (download/copy/open-in), matching the browser's
    // IMAGE cluster, plus the blank-fill swatch. (actOpenIn_ moved here from Projects.)
    tb->addWidget(makeToolSection("Image", {actOpen_, actSaveImage_, actCopyImage_, actOpenIn_}, {blankColorBtn_}));
    tb->addSeparator();
    // Projects = open editor list + save/open .stencil + live-sync, matching the browser's
    // PROJECTS cluster (layers / save / folder / refresh). Clear-project stays in the menu bar.
    tb->addWidget(makeToolSection("Projects", {actProjects_, actSaveProjectFile_, actOpenProjectFile_, actStencilLiveSync_}));
    tb->addSeparator();
    // Share = the browser's merged Servers + Links (connect to share/co-edit + image source links).
    tb->addWidget(makeToolSection("Share", {actConnect_, actLinks_}));
    tb->addSeparator();
    // Edit = adjust the current image + undo/redo (the browser's new Edit section).
    tb->addWidget(makeToolSection("Edit", {actCrop_, actRotateLeft_, actRotateRight_, actUndo_, actRedo_}));
    tb->addSeparator();
    // Draw = Start + Stop, mirroring the browser toolbar's Draw section (no New Line button there;
    // New Line stays on the Edit menu / Alt+N). The Line/Rect mode toggle sits in the Style row.
    tb->addWidget(makeToolSection("Draw", {actStartDraw_, actStopDraw_}));
    tb->addSeparator();
    // Zoom = the editable percent combo + a Fit-to-window button (browser parity — the browser's
    // zoom section ends with the fit icon). The combo replaces the browser's +/- steppers (type or
    // pick a preset). Fit button built here so it sits AFTER the combo, like the browser.
    auto* zoomFitBtn = new QToolButton(this);
    zoomFitBtn->setDefaultAction(actFit_);
    zoomFitBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    zoomFitBtn->setAutoRaise(true);
    zoomFitBtn->setIconSize(QSize(18, 18));
    tb->addWidget(makeToolSection("Zoom", {}, {zoom_, zoomFitBtn}));
    tb->addSeparator();
    // Settings = incognito + fullscreen (theme/shortcuts/help live in the menu bar).
    tb->addWidget(makeToolSection("Settings", {actIncognito_, actFullscreen_}));
  }

  // ── Project name field + inline-rename ✓/✗ (mirrors the browser topbar). The field shows the
  // active project's name and renames it inline, validated live: ✓ is enabled only for a changed,
  // valid (non-empty, ≤80, unique) name, with the reason on its tooltip when disabled. Enter = ✓,
  // Escape / click-away = ✗. Lives in the always-visible header row beside the "Controls" pill. ──
  void MainWindow::buildProjectNameGroup(QToolBar* tbName) {
    tbName->addWidget(new QLabel("Project: ", this));
    projectName_ = new QLineEdit(this);
    projectName_->setPlaceholderText("No project");
    projectName_->setToolTip(QString());   // no tooltip on the name field (the ✎ button has its own)
    projectName_->setMinimumWidth(150);
    projectName_->setMaximumWidth(300);
    // A QLineEdit is horizontally Expanding by default — in a toolbar that stretches it across the
    // whole row and shoves the ✎/🎨 far to the right. Make it content-sized so the name + icons
    // pack together on the left (a trailing spacer below absorbs the rest of the row).
    projectName_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    projectName_->setEnabled(false);
    projectName_->setReadOnly(true);  // browser-like: read-only until edit mode (✎ / double-click)
    tbName->addWidget(projectName_);
    // Browser-style affordances beside the name: a ✎ rename pencil (focuses + selects the field)
    // and a 🎨 colour icon (flat — NOT a filled swatch — opening choose / theme-default). Both
    // are themed line-art glyphs (styleActionIcons) and enable only with an active project.
    projectNameEdit_ = new QToolButton(this);
    projectNameEdit_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    projectNameEdit_->setAutoRaise(true);
    projectNameEdit_->setToolTip("Rename project");
    projectNameEdit_->setEnabled(false);
    projectNameEditAction_ = tbName->addWidget(projectNameEdit_);
    connect(projectNameEdit_, &QToolButton::clicked, this, [this] { enterNameEdit(); });
    projectColorBtn_ = new QToolButton(this);
    projectColorBtn_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    projectColorBtn_->setAutoRaise(true);
    projectColorBtn_->setToolTip("Project name colour — right-click to reset to default");
    projectColorBtn_->setEnabled(false);
    projectColorBtnAction_ = tbName->addWidget(projectColorBtn_);
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
    projectNameAcceptAction_ = tbName->addWidget(projectNameAccept_);
    projectNameAcceptAction_->setVisible(false);
    projectNameCancel_ = new QToolButton(this);
    projectNameCancel_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    projectNameCancel_->setToolTip("Cancel (Esc)");
    projectNameCancel_->setVisible(false);
    projectNameCancelAction_ = tbName->addWidget(projectNameCancel_);
    projectNameCancelAction_->setVisible(false);
    // Trailing expanding spacer: absorbs the rest of the row so the label + name + ✎/🎨 stay packed
    // together on the LEFT (no huge gap), instead of the name field stretching across the whole row.
    { auto* sp = new QWidget(this); sp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); tbName->addWidget(sp); }
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
    // Hover-reveal the ✎/🎨 group: watch Enter/Leave on the field AND both buttons so moving between
    // them counts as one hover region (handled in eventFilter → updateNameHover).
    projectNameEdit_->installEventFilter(this);
    projectColorBtn_->installEventFilter(this);
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
    allowFormulas_ = new QCheckBox("𝑓(x,y)", this);
    // Styled as an accent PILL toggle (theme.cpp QCheckBox#formulaPill): accent outline + text
    // when off, accent-filled with contrasting text when on — matching the browser toolbar.
    allowFormulas_->setObjectName("formulaPill");
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

  // Full-width "Image Size: W × H px" bar below the tool rows (browser parity with the
  // #image-info strip above the canvas), replacing the old top-right header placement.
  void MainWindow::buildImageInfoBar() {
    addToolBarBreak();
    auto* bar = addToolBar("Image Size");
    bar->setMovable(false);
    bar->setObjectName("imageInfoBar");
    bar->addWidget(imageSizeInfo_);   // left-aligned label; the bar spans the window width
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
    imageFilter_->addItem("Invert", "invert");
    imageFilter_->addItem("Contour", "contour");
    imageFilter_->addItem("Tint", "custom");
    imageFilter_->setToolTip(
        "Image filter: none, black & white, sepia, invert, contour, or tint");
    tb3->addWidget(imageFilter_);

    // Tint swatch (toolbar.js:30 #filterColor), hidden unless the "custom" filter
    // is selected. The QWidgetAction handle is toggled so the toolbar re-lays-out.
    filterColorBtn_ = new QToolButton(this);
    filterColorBtn_->setToolTip("Tint color");
    updateColorSwatch(filterColorBtn_, filterColorValue_);
    filterColorAct_ = tb3->addWidget(filterColorBtn_);
    filterColorAct_->setVisible(false);
    tb3->addSeparator();

    // Compare view combo (browser toolbar View section): hold the edit against the
    // untouched original. Kept in sync with the View → Compare submenu radio set.
    tb3->addWidget(new QLabel(" Compare: ", this));
    compareCombo_ = new QComboBox(this);
    compareCombo_->addItem("None", "none");
    compareCombo_->addItem("Original only", "original");
    compareCombo_->addItem(QString::fromUtf8("Split ↔ (vertical)"), "vertical");
    compareCombo_->addItem(QString::fromUtf8("Split ↕ (horizontal)"), "horizontal");
    compareCombo_->setToolTip(
        "Compare with the original (Alt+O cycles · hold Alt+Shift+O to peek)\n"
        "None — normal editing\n"
        "Original — original image only (crop + rotation; no filter, lines or points)\n"
        "Vertical split — left: original · right: current edit\n"
        "Horizontal split — top: original · bottom: current edit");
    tb3->addWidget(compareCombo_);
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
    // Compare view combo → route through the shared setter (syncs canvas + View submenu).
    connect(compareCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
              setCompareModeUi(compareCombo_->currentData().toString());
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
    if (filterColorAct_) filterColorAct_->setVisible(mode == "custom");
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
    setColorSwatch(btn, color);  // QToolButton derives from QAbstractButton
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
    notify_->success("Image loaded");
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
    if (dlg.exec() != QDialog::Accepted) return;
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
    if (loadLocalImageReset(path)) adoptCanvasAsLocalProject();
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
      notify_->info("Blank image canceled");
      return;
    }
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
    refreshActions();
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
    const bool custom = pageSizeValue() == "custom";
    if (customGroupAct_) customGroupAct_->setVisible(custom);
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

  // Validate fx/fy at input time and apply them (S11; drawingApp.js
  // validateAndApplyFormulas ~270). Invalid expressions show an inline error and
  // are not applied; valid ones persist and refresh the readout.
  void MainWindow::validateAndApplyFormulas() {
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
    // Accent-fill the Start button while a draw session is live (browser parity: start-drawing
    // gains the .active class). A dynamic property + repolish, so we don't make the action itself
    // checkable (which would add a stray check-mark to the Edit/context menus).
    if (startDrawBtn_ && startDrawBtn_->property("drawActive").toBool() != drawing) {
      startDrawBtn_->setProperty("drawActive", drawing);
      startDrawBtn_->style()->unpolish(startDrawBtn_);
      startDrawBtn_->style()->polish(startDrawBtn_);
    }
    // These are otherwise always enabled (they no-op internally when nothing applies);
    // the only gate is the read-only compare view.
    actNewLine_->setEnabled(!ro);
    actDeleteLast_->setEnabled(!ro);
    actClearAll_->setEnabled(!ro);
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
    if (actClearProject_)
      actClearProject_->setVisible(remoteSession_->link().address.isEmpty());
    updateProjectTitle();   // keep the window title + toolbar name field in sync
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
    auto subMenu = [&](const char* icon, const QString& title) {
      auto* m = new StayOpenMenu(title, &menu);
      menu.addMenu(m)->setIcon(themedIcon(QString::fromLatin1(icon), iconColor_, subIcon));
      return m;
    };

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
    QMenu* style = subMenu("palette", "Style");
    style->addAction(markerSizeAction_);
    style->addAction(thicknessAction_);
    style->addSeparator();
    style->addAction(actStyleSolid_);
    style->addAction(actStyleDashed_);
    style->addAction(actStyleDotted_);

    // Image Filter submenu (contextMenu.js:59-74).
    QMenu* filter = subMenu("image", "Image Filter");
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
    actSaveProjectFile_->setEnabled(hasImg);

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
    panelReopenBtn_->setFixedSize(28, 28);   // same rounded square as the panel-header collapse chevron
    panelReopenBtn_->setIconSize(QSize(18, 18));
    panelReopenBtn_->setToolTip(QString("Show panel (%1)").arg(hotkey("togglePointsList", "Alt+X")));
    panelReopenBtn_->setStyleSheet(panelToggleQss());
    connect(panelReopenBtn_, &QToolButton::clicked, this,
            [this] { if (actPanel_) actPanel_->setChecked(true); });
    panelReopenBtn_->hide();
    positionOverlayArrows();
    updatePanelReopenButton();
  }

  void MainWindow::positionOverlayArrows() {
    if (controlsPill_) {
      const QColor ic = palette().color(QPalette::WindowText);
      const bool tbShown = actToolbars_ ? actToolbars_->isChecked() : true;   // ↑ shown, ↓ collapsed
      controlsPill_->setIcon(themedIcon(tbShown ? "chevron-up" : "chevron-down", ic, 14));
    }
    updatePanelReopenButton();
  }

  void MainWindow::positionPanelReopenButton() {
    if (!panelReopenBtn_ || !scroll_) return;
    // Top-RIGHT of the canvas, flush inside the viewport's right edge so it sits to the LEFT of any
    // vertical scrollbar (viewport()->width() already excludes the scrollbar) — not centred, not
    // overlapping the scrollbar. A small top margin keeps it clear of the toolbar edge.
    QWidget* vp = scroll_->viewport();
    const QPoint tr = vp->mapTo(this, QPoint(vp->width(), 0));
    panelReopenBtn_->move(tr.x() - panelReopenBtn_->width(), tr.y() + 10);
  }

  void MainWindow::updatePanelReopenButton() {
    if (!panelReopenBtn_) return;
    // Only when the panel is fully hidden and we're not in fullscreen (which edge-hover-reveals it).
    const bool showBtn = selPanel_ && !selPanel_->isVisible() && !fsActive_;
    panelReopenBtn_->setVisible(showBtn);
    if (showBtn) {
      panelReopenBtn_->setIcon(themedIcon("chevron-left", palette().color(QPalette::WindowText), 18));
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
    panelAnim_ = new QVariantAnimation(this);
    panelAnim_->setDuration(280);
    panelAnim_->setEasingCurve(QEasingCurve::OutCubic);   // fast start, soft landing — smoother than InOut
    panelAnim_->setStartValue(from);
    panelAnim_->setEndValue(to);
    connect(panelAnim_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { selPanel_->setFixedWidth(v.toInt()); });
    connect(panelAnim_, &QVariantAnimation::finished, this, finish);
    panelAnim_->start(QAbstractAnimation::DeleteWhenStopped);
  }

  void MainWindow::setToolbarsShown(bool show, bool animate) {
    // The header row (Controls pill + project name) always stays — collapse only the tool rows,
    // mirroring the browser where the header keeps the pill/title while the body hides.
    QList<QToolBar*> bars;
    for (QToolBar* b : findChildren<QToolBar*>())
      if (b != headerToolbar_) bars.append(b);
    if (bars.isEmpty()) return;
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
    barsAnim_ = new QVariantAnimation(this);
    barsAnim_->setDuration(280);
    barsAnim_->setEasingCurve(QEasingCurve::OutCubic);   // fast start, soft landing — smoother than InOut
    barsAnim_->setStartValue(from);
    barsAnim_->setEndValue(to);
    connect(barsAnim_, &QVariantAnimation::valueChanged, this, [bars, this](const QVariant& v) {
      for (QToolBar* b : bars) b->setFixedHeight(v.toInt());   // pin min==max on every row
      positionOverlayArrows();
    });
    connect(barsAnim_, &QVariantAnimation::finished, this, [this, bars, show, release] {
      release();
      if (!show) for (QToolBar* b : bars) b->hide();
      barsAnim_ = nullptr;
      positionOverlayArrows();
    });
    barsAnim_->start(QAbstractAnimation::DeleteWhenStopped);
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
    if (logoBtn_) logoBtn_->setIcon(QIcon(makeLogoPixmap(24)));   // frame tracks the accent colour
    positionOverlayArrows();   // re-tint the Controls-pill chevron + the panel re-open tab

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
    set(actLinks_, "link");
    set(actConnect_, "server");
    set(actOpenIn_, "monitor");
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
    // Show Points / Show Lines are checkable toggles: leave them icon-less so the menu renders
    // its native check-mark for the on state (browser contextMenu.js parity — a check when shown,
    // nothing when hidden). An icon here would take the check column and mask the on/off state.
    if (actShowPoints_) actShowPoints_->setIcon(QIcon());
    if (actShowLines_) actShowLines_->setIcon(QIcon());
    // Arrow toggles (browser parity): a chevron to collapse the points panel (→, it's on the right)
    // and the toolbars (↑). The panel chevron flips ←/→ with its shown state in refreshActions.
    set(actPanel_, actPanel_ && actPanel_->isChecked() ? "chevron-right" : "chevron-left");
    set(actToolbars_, "chevron-up");   // top-menu (toolbars) show/hide, View menu only
    set(actFullscreen_, "maximize");
    set(actTooltip_, "message");
    set(actAllowFormulas_, "function");
    set(actUnitCm_, "ruler");
    set(actUnitIn_, "ruler");
    // Incognito: always the mask glyph (browser parity — the browser keeps the same icon and
    // just dims it when disabled). Qt auto-greys the icon for the disabled/locked state, so we
    // don't swap in a separate lock glyph.
    if (actIncognito_) actIncognito_->setIcon(themedIcon("incognito", iconColor, s));
    set(actSettings_, "gear");
    // Project / data
    set(actProjects_, "layers");          // browser projects-btn glyph (layers, not folder)
    set(actNewProject_, "file-text");
    set(actSaveProject_, "save");
    set(actSaveProjectFile_, "save");     // Projects toolbar: Save Project (.stencil)
    set(actOpenProjectFile_, "folder");   // Projects toolbar: Open Project (.stencil)
    set(actStencilLiveSync_, "refresh");  // Projects toolbar: live sync to file
    set(actClearProject_, "trash");
    set(actSaveSession_, "clipboard");
    set(actDownloadJson_, "download");
    set(actUploadJson_, "upload");
    set(actCopyLayout_, "copy");
    set(actPasteLayout_, "paste");
    set(actSaveImage_, "download");        // browser save-image glyph (download)
    set(actCopyImage_, "copy");
    set(actPasteImage_, "paste");
    // Help
    set(actInfo_, "info");
    set(actShortcuts_, "help");
    set(actQuit_, "power");
    // Context-menu extras
    set(actDrawModeToggle_, "rect");   // browser contextMenu.js parity (rect outline, not a pencil)
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
    // blankColorBtn_'s icon is a live colour swatch (set in updateProjectTitle), not a themed glyph.
    if (drawModeBtn_) {
      const bool rect =
          canvas_ && canvas_->drawMode() == CanvasWidget::DrawMode::Rect;
      drawModeBtn_->setIcon(
          themedIcon(rect ? "rect-filled" : "pencil", iconColor, 16));
    }
    restyleContextToggles(iconColor);  // theme-text (not accent) checkbox/radio indicators
  }

  // Recolour the context-menu hosted checkboxes/radios so their indicators use the theme TEXT
  // colour, matching the surrounding menu text rather than the app-wide accent (which the global
  // QSS applies to every other QCheckBox/QRadioButton). The check/dot glyphs are rasterised in
  // the text colour and cached on disk keyed by hex, so a theme switch regenerates them without
  // Qt serving a stale QSS-image cache. Applied per-widget so only these menu controls change.
  void MainWindow::restyleContextToggles(const QColor& textColor) {
    const QString hex = textColor.name().mid(1);  // "rrggbb"
    const QString checkPath = QDir::tempPath() + "/stencil-ctx-check-" + hex + ".png";
    const QString dotPath = QDir::tempPath() + "/stencil-ctx-dot-" + hex + ".png";
    if (!QFileInfo::exists(checkPath))
      themedIcon("check", textColor, 12).pixmap(12, 12).save(checkPath, "PNG");
    if (!QFileInfo::exists(dotPath)) {
      QPixmap dot(12, 12);
      dot.fill(Qt::transparent);
      {
        QPainter p(&dot);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(textColor);
        p.drawEllipse(3, 3, 6, 6);
      }  // painter destroyed before save
      dot.save(dotPath, "PNG");
    }
    const QString css =
        QStringLiteral(
            "QCheckBox::indicator,QRadioButton::indicator{width:15px;height:15px;"
            "border:1px solid %1;background:transparent;}"
            "QCheckBox::indicator{border-radius:4px;}"
            "QRadioButton::indicator{border-radius:8px;}"
            "QCheckBox::indicator:checked{image:url(\"%2\");}"
            "QRadioButton::indicator:checked{image:url(\"%3\");}")
            .arg(textColor.name(), checkPath, dotPath);
    QList<QWidget*> toggles = {tooltipEnableCheck_, ttPageCheck_, ttScreenCheck_,
                               ttCoordsCheck_, ctxAllowFormulas_};
    if (filterButtons_)
      for (QAbstractButton* b : filterButtons_->buttons()) toggles.append(b);
    for (QWidget* w : toggles)
      if (w) w->setStyleSheet(css);
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

  // Guard every user-initiated exit (Quit / Ctrl+Q / title-bar X all route through
  // QWidget::close()) with an "are you sure?" prompt. Ignoring the event cancels the
  // close. forceClose_ lets the load-failure auto-close paths skip the prompt.
  // Hand-built (not the QMessageBox::question helper) to drop the oversized default
  // question-mark glyph and use clear "Quit"/"Cancel" action buttons, Cancel default.
  void MainWindow::closeEvent(QCloseEvent* event) {
    if (!forceClose_) {
      QMessageBox box(this);
      box.setWindowTitle("Quit Stencil");
      box.setIcon(QMessageBox::NoIcon);
      // Rich-text header, one size larger than the informative subline and tinted with
      // the current brand accent (the same violet/… the rest of the app uses).
      box.setTextFormat(Qt::RichText);
      box.setText(QString("<div style='font-size:17pt; font-weight:600; color:%1;'>"
                          "Quit Stencil?</div>")
                      .arg(accentPrimary(settings_.accentColor).name()));
      box.setInformativeText("Are you sure you want to quit?");
      QPushButton* quitBtn = box.addButton("Quit", QMessageBox::AcceptRole);
      QPushButton* cancelBtn = box.addButton("Cancel", QMessageBox::RejectRole);
      box.setDefaultButton(cancelBtn);   // safe default: a stray Enter/Esc keeps the app open
      // A modest width bump over QMessageBox's tight default — enough to breathe without
      // the wide empty gutter a larger spacer leaves between the text and the buttons.
      if (auto* grid = qobject_cast<QGridLayout*>(box.layout()))
        grid->addItem(new QSpacerItem(300, 0, QSizePolicy::Minimum, QSizePolicy::Fixed),
                      grid->rowCount(), 0, 1, grid->columnCount());
      box.exec();
      if (box.clickedButton() != quitBtn) {
        event->ignore();
        return;
      }
    }
    QMainWindow::closeEvent(event);
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

    ProjectsDialog dlg(projectList_, nowMs(), connections_, buildProjectThumbs(),
                       unitFormat(), this);
    dlg.setDragZones(projectZones_);   // the main-window drag-out zone overlay (open/new-window/remove)
    if (dlg.exec() != QDialog::Accepted) return;

    using Action = ProjectsDialog::Action;
    // Open CONFIRM lives here — AFTER the dialog closed — not inside the drag release (where a
    // QMessageBox got dismissed by that same release, so nothing opened and the row snapped back).
    // Mirrors the Delete confirm below, which is why Remove worked while Open didn't.
    auto confirmOpen = [this](const QString& id, bool newWindow) -> bool {
      const Project* pr = findProject(id.toStdString());
      const QString nm = pr ? QString::fromStdString(pr->meta.name) : QStringLiteral("this project");
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
          c->deleteProjectAsync(id, [](bool) {});  // fire-and-forget; local list refresh is independent
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
              QString("Are you sure? This removes all %1 local project(s) and cannot be undone. "
                      "Server projects are not affected.")
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
                                 .arg(QString::fromStdString(pr->meta.name))
                           : QString("\"%1\" expiration updated")
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
    refreshActions();
    fitToWindow();   // fit the opened project to the window (matches the browser)
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

    // Read all of `path` into `out`; false (leaving `out` untouched) on open failure.
    bool readFileBytes(const QString& path, QByteArray& out) {
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly)) return false;
      out = f.readAll();
      return true;
    }
    // Overwrite `path` with `data` (truncating); false on open failure.
    bool writeFileBytes(const QString& path, const QByteArray& data) {
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
      f.write(data);
      return true;
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
        if (!silent)
          notify_->success(QString("Opened \"%1\" from %2")
                               .arg(meta.name.isEmpty() ? QStringLiteral("Untitled") : meta.name,
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
      win->forceClose_ = true;   // auto-close a failed load without a quit prompt
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
      bool known = false;
      for (const auto& s : stencil::net::connectionStore::loadSavedServers()) {
        if (stencil::net::ServerClient::normalizeBase(s.url) == url) {
          token = s.token;
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
      if (!mgr->connectTo(url, token, err)) {
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
    if (dlg.exec() != QDialog::Accepted) return;
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
    notify_->info("Opening…");
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
    fitToWindow();
    notify_->success("Opened project " + pf.name);
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

  QByteArray MainWindow::buildStencilBytes() {
    if (!canvas_->hasImage()) return {};
    const QImage& orig = canvas_->originalImage();
    QByteArray png;
    QString ext = QStringLiteral("png");
    if (!sourceBytes_.isEmpty()) {
      png = sourceBytes_;                                  // untouched original (lossless)
      if (!sourceExt_.isEmpty()) ext = sourceExt_;
    } else {
      QBuffer buf(&png);                                  // synthetic original — encode from pixels
      buf.open(QIODevice::WriteOnly);
      orig.save(&buf, "PNG");
    }
    fileStore::ProjectFileData pf;
    pf.name = projectBaseName();
    pf.imageExt = ext;
    pf.imageBytes = png;
    pf.imageWidth = orig.width();
    pf.imageHeight = orig.height();
    pf.source = currentSource_;
    pf.resource = currentResource_;
    if (const Project* pr = findProject(activeProjectId_.toStdString())) {
      pf.color = QString::fromStdString(pr->meta.color);
      for (const auto& k : pr->meta.keywords) pf.keywords << QString::fromStdString(k);
      pf.blank = pr->meta.blank;
      pf.blankColor = QString::fromStdString(pr->meta.blankColor);
    }
    pf.layout = fileStore::buildLayoutJson(
        canvas_->imageWidth(), canvas_->imageHeight(), canvas_->allLines(),
        settings_.imageFilter, settings_.filterColor,
        canvas_->cropRect(), canvas_->rotationQuarters(), currentLayoutMeta());
    pf.hasTheme = true;
    pf.themeMode = resolveDark(settings_.themeMode) ? "dark" : "light";
    pf.themeAccent = settings_.accentColor;
    return fileStore::buildProjectFile(pf);
  }

  void MainWindow::saveProjectFileAs() {
    if (!canvas_->hasImage()) {
      notify_->error("Load an image first");
      return;
    }
    const QString suggested = projectBaseName() + ".stencil";
    const QString path = QFileDialog::getSaveFileName(
        this, "Save project", suggested, "Stencil project (*.stencil)");
    if (path.isEmpty()) return;
    const QByteArray out = buildStencilBytes();
    if (!writeFileBytes(path, out)) {
      notify_->error("Could not write the project file");
      return;
    }
    linkStencilFile(path, out);   // this file becomes the project's live-sync target
    notify_->success("Project saved");
  }

  // ── .stencil live sync ───────────────────────────────────────────────────────
  namespace {
    // Union two line lists, de-duplicating by the compact JSON of each line (a merge that keeps
    // both editors' annotations without duplicating a round-tripped twin — mirrors browser mergeLines).
    core::Lines mergeLinesUnion(const core::Lines& base, const core::Lines& extra) {
      core::Lines out = base;
      QSet<QString> seen;
      auto keyOf = [](const core::Line& l) {
        return QString::fromUtf8(QJsonDocument(fileStore::lineToJson(l)).toJson(QJsonDocument::Compact));
      };
      for (const auto& l : base) seen.insert(keyOf(l));
      for (const auto& l : extra) {
        const QString k = keyOf(l);
        if (!seen.contains(k)) { out.push_back(l); seen.insert(k); }
      }
      return out;
    }
  }  // namespace

  void MainWindow::linkStencilFile(const QString& path, const QByteArray& baseline) {
    stencilLink_ = path;
    stencilBaseline_ = baseline;
    if (!stencilWatcher_) {
      stencilWatcher_ = new QFileSystemWatcher(this);
      connect(stencilWatcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString&) { onStencilFileChanged(); });
    }
    if (!stencilWatcher_->files().isEmpty()) stencilWatcher_->removePaths(stencilWatcher_->files());
    if (stencilLiveSync_ && !path.isEmpty()) stencilWatcher_->addPath(path);
    if (actStencilLiveSync_) actStencilLiveSync_->setEnabled(!stencilLink_.isEmpty());
  }

  void MainWindow::scheduleStencilAutosave() {
    if (stencilLink_.isEmpty() || !stencilLiveSync_ || stencilApplying_) return;
    if (!stencilAutosaveTimer_) {
      stencilAutosaveTimer_ = new QTimer(this);
      stencilAutosaveTimer_->setSingleShot(true);
      connect(stencilAutosaveTimer_, &QTimer::timeout, this, &MainWindow::flushStencilAutosave);
    }
    stencilAutosaveTimer_->start(800);
  }

  void MainWindow::flushStencilAutosave() {
    if (stencilLink_.isEmpty() || !stencilLiveSync_ || !canvas_->hasImage()) return;
    const QByteArray cur = buildStencilBytes();
    if (cur == stencilBaseline_) return;   // no local change
    // Race: if the file changed externally since our baseline, route to the change handler
    // (apply / prompt) instead of clobbering it — reusing `cur` so it needn't rebuild them.
    QByteArray ext;
    if (readFileBytes(stencilLink_, ext) && ext != stencilBaseline_) {
      onStencilFileChanged(cur);
      return;
    }
    writeStencilNow(cur);   // reuse the bytes we just built (no second PNG re-encode)
    notify_->info("Synced to file");
  }

  void MainWindow::writeStencilNow(const QByteArray& prebuilt) {
    if (stencilLink_.isEmpty()) return;
    const QByteArray cur = prebuilt.isEmpty() ? buildStencilBytes() : prebuilt;
    QFile wf(stencilLink_);
    if (!wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    wf.write(cur);
    wf.close();
    stencilBaseline_ = cur;
    // QFileSystemWatcher drops a path once its file is replaced — re-add so we keep watching.
    if (stencilWatcher_ && !stencilWatcher_->files().contains(stencilLink_)) stencilWatcher_->addPath(stencilLink_);
  }

  void MainWindow::onStencilFileChanged(const QByteArray& prebuilt) {
    if (stencilLink_.isEmpty()) return;
    if (stencilWatcher_ && !stencilWatcher_->files().contains(stencilLink_)) stencilWatcher_->addPath(stencilLink_);
    QByteArray ext;
    if (!readFileBytes(stencilLink_, ext)) return;
    if (ext.isEmpty() || ext == stencilBaseline_) return;   // no external change vs our baseline
    const QByteArray cur = prebuilt.isEmpty() ? buildStencilBytes() : prebuilt;
    if (cur == stencilBaseline_) {                          // no local edits → apply theirs
      applyStencilExternal(ext);
      return;
    }
    // Conflict: both changed since the baseline — prompt (mirrors the browser 3-way choice).
    QMessageBox box(this);
    box.setWindowTitle(tr("File changed"));
    box.setText(tr("“%1” was changed outside the app and conflicts with your unsaved edits.")
                    .arg(QFileInfo(stencilLink_).fileName()));
    QPushButton* theirs = box.addButton(tr("Take file’s version"), QMessageBox::AcceptRole);
    QPushButton* merge = box.addButton(tr("Merge lines"), QMessageBox::ActionRole);
    box.addButton(tr("Keep mine (overwrite file)"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == theirs) applyStencilExternal(ext);
    else if (box.clickedButton() == merge) applyStencilExternal(ext, /*merge=*/true);
    else writeStencilNow(cur);   // keep mine → overwrite the file (reuse the bytes we built)
  }

  void MainWindow::applyStencilExternal(const QByteArray& text, bool merge) {
    fileStore::ProjectFileData pf;
    QString err;
    if (!fileStore::parseProjectFile(text, pf, &err)) {
      notify_->error("Could not read the changed project file");
      return;
    }
    QImage img;
    if (!img.loadFromData(pf.imageBytes)) return;
    QJsonObject layout = pf.layout;
    if (merge) {
      int w = 0, h = 0;
      const core::Lines fileLines = fileStore::parseLayoutJson(pf.layout, w, h);
      layout["lines"] = fileStore::linesToJson(mergeLinesUnion(fileLines, canvas_->allLines()));
    }
    stencilApplying_ = true;
    loadImageWithLayout(img, layout, pf.imageBytes, pf.imageExt);
    stencilApplying_ = false;
    if (merge) {
      writeStencilNow();   // push the merged result back to the file
      notify_->success("Merged with file");
    } else {
      stencilBaseline_ = text;
      notify_->success("Reloaded from file");
    }
    refreshActions();
  }

  void MainWindow::toggleStencilLiveSync(bool on) {
    stencilLiveSync_ = on;
    if (on && !stencilLink_.isEmpty()) {
      QFile rf(stencilLink_);
      if (rf.open(QIODevice::ReadOnly)) { stencilBaseline_ = rf.readAll(); rf.close(); }
      if (stencilWatcher_) stencilWatcher_->addPath(stencilLink_);
      scheduleStencilAutosave();   // push any pending local edits
      notify_->success(tr("Live sync on — auto-saving to %1").arg(QFileInfo(stencilLink_).fileName()));
    } else {
      if (stencilWatcher_ && !stencilWatcher_->files().isEmpty()) stencilWatcher_->removePaths(stencilWatcher_->files());
      if (on) notify_->info("Open or save a .stencil file first to enable live sync");
      else notify_->info("Live sync off");
    }
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
        notify_->success("Opened from Stencil");
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
    notify_->success("Image opened");

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
    if (!win->loadProjectIntoCanvas(id)) { win->forceClose_ = true; win->close(); }
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
      box.addButton(QMessageBox::Cancel);
      box.exec();
      if (box.clickedButton() == hereBtn) openHere();
      else if (box.clickedButton() == newBtn) openNew();
      return;
    }
    openHere();
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
    if (announce) notify_->success(QString("Created \"%1\"").arg(name));
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
    fileStore::saveProjects(projectList_);
    refreshDockMenu();  // bump it to the top of the Dock "recent" list
    notify_->success(
        QString("Saved to \"%1\"").arg(QString::fromStdString(pr->meta.name)));
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
    notify_->info(hasProject ? "Project cleared" : "Editor cleared");
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
    canvas_->clearImage();
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
    notify_->success(QString("Renamed to \"%1\"").arg(name));
    return true;
  }

  // Header-row "Image Size: W × H px" (+ "· blank"), or a neutral hint when no image is loaded.
  // Always visible — the header row never collapses — mirroring the browser's #image-info bar.
  void MainWindow::updateImageSizeInfo() {
    if (!imageSizeInfo_) return;
    if (canvas_ && canvas_->hasImage()) {
      const bool isBlank = !blankColor_.isEmpty();
      imageSizeInfo_->setText(QString("Image Size: %1 × %2 px%3")
                                  .arg(canvas_->imageWidth())
                                  .arg(canvas_->imageHeight())
                                  .arg(isBlank ? QStringLiteral("  ·  blank") : QString()));
    } else {
      imageSizeInfo_->setText(QStringLiteral("No image loaded"));
    }
  }

  // Mini line-chart logo — a QPainter port of the browser's app-logo SVG (toolbar.js): a dark rounded
  // square with an ACCENT-coloured frame, an inner darker square, and a yellow polyline over four
  // dots. Only the frame tracks the accent (like the browser), so it never looks garish. Repainted on
  // theme/accent change from applyTheme.
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
    const QPointF pts[4] = {QPointF(16 * u, 46 * u), QPointF(27 * u, 24 * u),
                            QPointF(38 * u, 38 * u), QPointF(50 * u, 18 * u)};
    QPen line(QColor("#FFFF00"));
    line.setWidthF(3.5 * u);
    line.setCapStyle(Qt::RoundCap);
    line.setJoinStyle(Qt::RoundJoin);
    p.setPen(line);
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(pts, 4);
    p.setBrush(QColor("#FFFF00"));
    p.setPen(QPen(QColor("#000000"), 1.25 * u));
    for (const auto& pt : pts) p.drawEllipse(pt, 3.4 * u, 3.4 * u);
    return pm;
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
    // ✎ rename + 🎨 colour reveal only while the cursor is over the name group (browser: the topbar
    // shows them on hover), or while editing they're replaced by ✓/✗ anyway.
    if (projectNameEditAction_) projectNameEditAction_->setVisible(editable && !nameEditing_ && nameHover_);
    if (projectColorBtnAction_) projectColorBtnAction_->setVisible(editable && !nameEditing_ && nameHover_);
    // Blank-colour button: shown only when this session is a blank image (recolourable), regardless
    // of whether it's a saved/editable project (in-memory recolour works for unsaved blanks too).
    // Paint its icon as a live swatch of the current fill colour.
    if (blankColorBtn_) {
      const bool showBlank = !blankColor_.isEmpty() && !nameEditing_;
      blankColorBtn_->setVisible(showBlank);   // now a plain layout widget, gated directly
      if (showBlank) {
        QPixmap sw(14, 14);
        QColor c(blankColor_);
        sw.fill(c.isValid() ? c : QColor("#ffffff"));
        blankColorBtn_->setIcon(QIcon(sw));
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

  bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    // Escape leaves fullscreen (browser parity). Handled from the APP-wide filter so it fires no
    // matter which widget (or native macOS view) holds focus — keyPressEvent / a shortcut both miss
    // it there. Gated on our own fsActive_ flag (isFullScreen() is unreliable on macOS). Catch both
    // KeyPress and ShortcutOverride (sent first if any widget claims Escape) so nothing swallows it.
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && fsActive_) {
      toggleFullscreen();
      return true;
    }
    // Zoom field → open the preset list without the separate arrow. Trigger on the click's
    // mouse-RELEASE (not press/focus): showing the popup during the press cycle lets the pending
    // release land outside it and immediately dismiss it (macOS), so it just flashed. Tab/keyboard
    // focus opens it too. The field stays editable, so the user can still type over the popup.
    if (zoom_ && obj == zoom_->lineEdit()) {
      const auto openPopup = [this] {
        QTimer::singleShot(0, this, [this] {
          if (zoom_ && zoom_->lineEdit()->hasFocus() && !zoom_->view()->isVisible()) zoom_->showPopup();
        });
      };
      if (event->type() == QEvent::MouseButtonRelease &&
          static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        openPopup();
      } else if (event->type() == QEvent::FocusIn) {
        const auto reason = static_cast<QFocusEvent*>(event)->reason();
        if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
            reason == Qt::ShortcutFocusReason)
          openPopup();
      }
      return false;   // never consume — the field's caret / typing must behave normally
    }
    // Logo double-click → custom theme-colour picker (browser parity). Cancels the pending single-
    // click accent-cycle first, then opens the non-native colour dialog seeded with the current accent.
    if (obj == logoBtn_ && event->type() == QEvent::MouseButtonDblClick) {
      if (logoClickTimer_) logoClickTimer_->stop();
      const QColor cur = accentPrimary(settings_.accentColor);
      const QColor c = QColorDialog::getColor(cur, this, "Theme colour",
                                              QColorDialog::DontUseNativeDialog);
      if (c.isValid()) {
        auto next = settings_;
        next.accentColor = c.name();   // store as hex → custom accent (accentPrimary handles it)
        applySettings(next, true);
      }
      return true;
    }
    // Zoom over the empty margin around a zoomed-out image (the viewport, not the
    // canvas). Mirrors CanvasWidget's Ctrl+wheel / pinch zoom; the event position is
    // already in viewport coordinates, which is what setZoomAnchored wants.
    if (scroll_ && obj == scroll_->viewport()) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Resize) { positionOverlayArrows(); positionPanelReopenButton(); }
      if (t == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
          const QPoint d = we->angleDelta();
          const int delta = d.y() != 0 ? d.y() : d.x();
          if (delta != 0) {
            const double step = (we->modifiers() & Qt::ShiftModifier) ? 0.3 : 0.1;
            setZoomAnchored(canvas_->scale() + (delta > 0 ? step : -step),
                            we->position().toPoint());
            return true;
          }
        }
        // Plain wheel over the margin → let the scroll area scroll.
      } else if (t == QEvent::NativeGesture) {
        auto* g = static_cast<QNativeGestureEvent*>(event);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
          const double factor = 1.0 + g->value();
          if (factor > 0.0 && factor != 1.0)
            setZoomAnchored(canvas_->scale() * factor, g->position().toPoint());
          return true;
        }
      }
    }
    // Hover-reveal for the name group: any Enter/Leave on the field or the ✎/🎨 buttons recomputes
    // hover (deferred so underMouse() settles — moving field→button stays "hovered", no flicker).
    if (obj == projectName_ || obj == projectNameEdit_ || obj == projectColorBtn_) {
      const QEvent::Type t = event->type();
      if (t == QEvent::Enter || t == QEvent::Leave)
        QTimer::singleShot(0, this, [this] { updateNameHover(); });
    }
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
      notify_->error("Invalid colour");
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
    // Qt's own dialog (not the OS-native one) so it matches the project-name colour picker.
    const QColor c = QColorDialog::getColor(init, this, "Blank background colour",
                                            QColorDialog::DontUseNativeDialog);
    if (!c.isValid()) return;
    // Regenerate the solid fill at the current size, KEEPING the drawn lines (a separate overlay).
    const core::Lines keep = canvas_->lines();
    QImage img(canvas_->imageWidth(), canvas_->imageHeight(), QImage::Format_RGB32);
    img.fill(c);
    canvas_->loadFromImage(img);
    setSourceBytes({}, {});  // recoloured blank is synthetic → re-encode on bundle
    if (!keep.empty()) canvas_->setLines(keep);
    blankColor_ = c.name();
    // Persist the new fill into the active local project's meta + raster so a reopen shows it.
    // (A server-linked session pushes the recoloured original on the next Save.)
    if (Project* pr = findProject(activeProjectId_.toStdString())) {
      pr->meta.blankColor = blankColor_.toStdString();
      pr->meta.blank = true;
      if (!pr->imagePath.isEmpty()) canvas_->originalImage().save(pr->imagePath, "PNG");
      fileStore::saveProjects(projectList_);
    }
    refreshActions();
    notify_->success("Blank recoloured");
  }

  void MainWindow::setProjectColorById(const QString& id, const QString& serverUrl,
                                       const QString& color, std::function<void(bool)> done) {
    const auto norm = normalizeProjectColor(color);
    if (!norm) {
      notify_->error("Invalid colour");
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
              notify_->error(QString("Colour update failed: %1").arg(c->lastError()));
              if (done) done(false);
              return;
            }
            if (remoteSession_->link().id == id && remoteSession_->link().address == serverUrl)
              remoteSession_->link().version = newVersion;
            notify_->success(n.isEmpty() ? QStringLiteral("Colour reset to theme default")
                                         : QString("Colour set to %1").arg(n));
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
    notify_->success(norm->isEmpty() ? QStringLiteral("Colour reset to theme default")
                                     : QString("Colour set to %1").arg(*norm));
    if (done) done(true);
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
    // Cursor off the canvas: show nothing when an image is loaded (no "Ready" filler,
    // matching the browser), only the idle hint while there's no image at all.
    status_->setText(canvas_->hasImage()
                         ? QString()
                         : QStringLiteral("Open an image — or create a blank one — to begin"));
  }

}
