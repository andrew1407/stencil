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
#include "deepLink.hpp"
#include "fetchGuard.hpp"
#include "displayName.hpp"
#include "openImageDialog.hpp"
#include "../support/localPath.hpp"
#include "../support/rowWork.hpp"    // support::forEachSlice() — parallel thumb decode
#include "openInDialog.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "overlayScrollArea.hpp"
#include "dropZonesOverlay.hpp"
#include "incognitoOverlay.hpp"
#include "projectDragZones.hpp"
#include "cropGeometry.hpp"
#include "imageFilter.hpp"
#include "pageMetrics.hpp"
#include "cropDialog.hpp"
#include "tooltipRows.hpp"
#include "zoomPan.hpp"
#include "guiHelpers.hpp"
#include "menuHotkeys.hpp"
#include "menuReveal.hpp"
#include "menuShimmer.hpp"
#include "modalReveal.hpp"
#include "searchCombo.hpp"
#include "controlsPill.hpp"
#include "iconSet.hpp"
#include "numericInput.hpp"
#include "infoDialog.hpp"
#include "launchOptions.hpp"
#include "linksDialog.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
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
#include "selectedLineBar.hpp"
#include "assistantSettingsDialog.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "theme.hpp"
#include "mainWindowShared.hpp"
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
#include "../support/appTooltip.hpp"           // the fading control tooltip
#include "../support/disintegrateOverlay.hpp"  // the canvas scatters when cleared
#include "../support/controlSwap.hpp"         // checkbox particles + combo value swap
#include "../support/controlReveal.hpp"        // a group of fields comes and goes as sand
#include "../support/wrapRow.hpp"               // the tool rows wrap, so their height follows the width
#include "../support/dockGrip.hpp"             // animated canvas↔panel separator grip
#include "../support/modalChrome.hpp"          // confirmModal — the browser-styled question
#include "../support/iconMotion.hpp"          // the per-icon hover motion
#include "../support/hoverSlide.hpp"          // the row hover slide (browser translateX)
#include "../support/shimmerOverlay.hpp"      // the shared hover sweep
#include <QHBoxLayout>
#include <QLayout>
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
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
#include <QDockWidget>
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
#include <memory>
#include <optional>

namespace stencil::gui {

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

    setupCanvasArea();
    installWindowFilters();

    setupDocks();

    setupChatDock();
    installPanelShimmers();

    setupOverlaysAndStatus();

    setupPageAndZoomControls();

    setupSyncControllers();

    buildActions();
    buildContextActions();  // nested context-menu submenu actions
    buildMenus();
    buildToolbar();
    wireExportOptionsPopups();  // needs the copy/save-image buttons buildToolbar() just made
    buildOverlayArrows();   // sync the Controls-pill chevron glyph (after the toolbar exists)
    bindRevealAnchors();    // every action records where its dialog should fly from

    // Control tooltips FADE (support/appTooltip.hpp) instead of Qt's snapping QTipLabel.
    // App-wide and idempotent, so a second window installs nothing new.
    installAppTooltips();

    // wiring — (after buildToolbar so the referenced widgets/actions exist)
    wireSignals();

    restorePersistedState(restoreLast);
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

  // hotkeys map (ported from browser/js/config/hotkeysConfig.json)
  // Defaults + labels from the embedded config, then user overrides layered on
  // top (override wins), mirroring the browser STORAGE_KEYS.hotkeys merge.
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
        hotkeyOrder_.append(id);
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
      hotkeyOrder_.append(c.id);
    }
    const auto overrides = fileStore::loadHotkeys();
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
      hotkeys_.insert(it.key(), it.value());
  }

  // Signal wiring extracted from the ctor. The connect() ORDER is observable
  // (e.g. the allowFormulas_ handler drives actAllowFormulas_; units_.customW/units_.customH
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
        pop_.dialogAnchor.clear();       // the rect below is the origin, not any icon
        pop_.dialogAnchorRect = card;
      }
      openImageDialog(/*startBlank=*/true);
    });
    // Reflect drawing mode in the Start/Stop actions.
    connect(canvas_, &CanvasWidget::drawingModeChanged, this,
            &MainWindow::refreshActions);
    connect(canvas_, &CanvasWidget::hoverDetail, this,
            &MainWindow::onHoverDetail);
    connect(canvas_, &CanvasWidget::hoverLeft, this,
            [this] { hideHoverTooltip(); });
    // Off the canvas there is nothing to read out: clear the coord bar and re-arm
    // the hover cache so unit/page refreshes don't repaint the stale numbers.
    connect(canvas_, &CanvasWidget::canvasLeft, this, [this] {
      lastHoverX_ = std::numeric_limits<double>::quiet_NaN();
      lastHoverY_ = std::numeric_limits<double>::quiet_NaN();
      updateStatusIdle();
    });
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
    // Page size + custom inputs. Index-based (not text): the editable
    // search field mutates the text on every keystroke, but a page change is
    // only a change of the selected item.
    connect(units_.pageSize, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onPageSizeChanged(); });
    connect(units_.customW, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
              // Spinboxes are edited in the active unit; store the model in cm.
              settings_.customPageWidth = v / unitFormat().factor;
              persistSettings();
              onHovered(lastHoverX_, lastHoverY_);
              onSelectionChanged();  // refresh panel cm
              remoteSync_->scheduleRemotePush();  // page format rides the layout — push it to peers
            });
    connect(units_.customH, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
              settings_.customPageHeight = v / unitFormat().factor;
              persistSettings();
              onHovered(lastHoverX_, lastHoverY_);
              onSelectionChanged();  // refresh panel cm
              remoteSync_->scheduleRemotePush();
            });
    // Formula controls. The toolbar checkbox is the single source of
    // truth; the View ▸ Allow Formulas action just drives it (and is kept in
    // sync here), so the feature stays reachable when the toolbar overflows.
    connect(allowFormulas_, &QCheckBox::toggled, this, [this](bool on) {
      settings_.allowFormulas = on;
      revealControls(formulaGroup_, on);
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

    // Hover cross-highlight (browser parity, both directions). List rows → canvas
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

    // "Selected Line:" bar (above the canvas) → canvas mutators (Step 10).
    // Mirrors browser/js/core/drawingApp.js:181-195 applySelectionChange /
    // applyFill / deselectLine wiring. No delete here (browser parity — the bar
    // carries no Delete button); that stays Alt+Delete / actDeleteLine_ / the
    // Lines tab's own row 🗑.
    connect(selectedLineBar_, &SelectedLineBar::lineColorChanged, this,
            [this](const QString& v, bool preview) { canvas_->setSelectedLineColor(v, preview); });
    connect(selectedLineBar_, &SelectedLineBar::linePointColorChanged, this,
            [this](const QString& v, bool preview) { canvas_->setSelectedLinePointColor(v, preview); });
    connect(selectedLineBar_, &SelectedLineBar::lineThicknessChanged, this,
            [this](int t) { canvas_->setSelectedLineThickness(t); });
    connect(selectedLineBar_, &SelectedLineBar::linePointSizeChanged, this,
            [this](int m) { canvas_->setSelectedLinePointSize(m); });
    connect(selectedLineBar_, &SelectedLineBar::lineStyleChanged, this,
            [this](const QString& s) { canvas_->setSelectedLineStyle(s); });
    connect(selectedLineBar_, &SelectedLineBar::lineFillChanged, this,
            [this](const QString& v, bool preview) { canvas_->setSelectedLineFill(v, preview); });
    connect(selectedLineBar_, &SelectedLineBar::unchainRequested, this,
            [this] { canvas_->unchainSelectedLine(); });
    connect(selectedLineBar_, &SelectedLineBar::deselectRequested, this,
            [this] { canvas_->deselect(); });
    // Anything the canvas did on its own that deserves a word (chainEdit's unchain).
    connect(canvas_, &CanvasWidget::statusMessage, this,
            [this](const QString& text) { notify_->success(text); });
    // Panel header chevron → hide the panel (routes through actPanel_ so the View menu / Alt+X and
    // the re-open tab stay in sync). The animated slide runs from setPanelShown.
    connect(selPanel_, &SelectionPanel::collapseRequested, this,
            [this] { if (actPanel_) actPanel_->setChecked(false); });
    selPanel_->setToggleHint(hotkey("togglePointsList", "Alt+X"));   // shortcut in the chevron tooltip

    // Lines tab (SelectionPanel) → canvas index-keyed selection/removal.
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
    syncExportActions();
    actPasteImage_->setEnabled(true);  // dispatch notifies "Load an image first"
    actCopyLayout_->setEnabled(hasLines);
    actDownloadJson_->setEnabled(hasLines);
    actPasteLayout_->setEnabled(hasImg);
    actUploadJson_->setEnabled(hasImg);
    actSaveProjectFile_->setEnabled(hasImg);

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
      if (canvas_->hasImage()) {
        ConfirmSpec spec;
        spec.title = tr("Replace image");
        spec.message = tr("Replace current image with the pasted image?");
        spec.confirmLabel = tr("Replace");
        spec.confirmIcon = QStringLiteral("refresh");
        if (!confirmModal(this, spec)) {
          notify_->info("Image paste canceled");  // drawingApp.js:568
          return;
        }
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

  // The chat gear's dedicated dialog: provider/base URL/model/API key/server
  // rows (llm-contract.md §5), commit/discard — same as the browser's own
  // llmSettingsModal.js. The (live-apply) Settings dialog just links here.
  void MainWindow::openAssistantSettings() {
    // The gear that raises this sits INSIDE the dock's "…" menu, which has already
    // closed by now — so the flight belongs to the "…" trigger itself, both ways. A
    // hidden dock leaves no anchor and the window falls from above instead.
    openAssistantSettingsFrom(chatDock_ ? chatDock_->moreButton() : nullptr);
  }

  void MainWindow::openAssistantSettingsFrom(QWidget* anchor, const QRect& anchorRect) {
    AssistantSettingsDialog dlg(settings_, this);
    // Its own chord closes it again and another window's chord swaps to that window,
    // as for every toolbar window (execMaybePopover) — this one is plain exec()'d.
    wireWindowSwitching(dlg, pop_.dialogActions, actAssistantSettings_);
    support::revealDialog(dlg, anchor, anchorRect);
    if (dlg.exec() == QDialog::Accepted) {
      applySettings(dlg.result(), true);
    }
  }

  void MainWindow::openSettings() {
    SettingsDialog dlg(settings_, this);
    // execMaybePopover, not exec(): every other dialog-opening icon grows its window out
    // of the icon (and answers dblclick/right-click with the compact anchored shape).
    // Visuals live in here, so this one was the odd one out.
    // Live-apply: every row persists itself as it changes; no Save/Cancel.
    dlg.setOnChange([this](const Settings& s) { applySettings(s, true); });
    connect(&dlg, &SettingsDialog::visualsReset, this,
            [this] { notify_->success(QStringLiteral("Visual defaults reset")); });
    execMaybePopover(dlg, actSettings_);
    // Settle-up: catches a field left mid-edit. Skipped when the result matches what
    // the live-apply already applied — the common close costs no extra full pass/save.
    if (fileStore::settingsToJson(dlg.result()) != fileStore::settingsToJson(settings_))
      applySettings(dlg.result(), true);
  }

  // The popover overlay's motion, matching the app's dialog reveal (modalReveal.cpp),
  // ×1.5 (too brisk).
  static constexpr int kPopoverOpenMs = 450;
  static constexpr int kPopoverCloseMs = 360;

  int MainWindow::execMaybePopover(QDialog& dlg, QAction* opener) {
    wireWindowSwitching(dlg, pop_.dialogActions, opener);
    // Park the originals while the dialog owns those chords, and put them back after.
    QList<QPair<QAction*, Qt::ShortcutContext>> parked;
    for (QAction* a : pop_.dialogActions) {
      if (!a || a->shortcut().isEmpty()) continue;
      parked.append({a, a->shortcutContext()});
      a->setShortcutContext(Qt::WidgetShortcut);
    }
    const QScopeGuard restore([&] {
      for (const auto& [a, ctx] : parked) a->setShortcutContext(ctx);
    });
    QWidget* anchor = pop_.anchor.data();
    pop_.anchor.clear();
    if (!anchor) {
      // Ordinary centred window: it still grows out of the icon that opened it — or,
      // when that icon is hidden, out of the menu row that was clicked.
      support::revealDialog(dlg, pop_.dialogAnchor.data(), pop_.dialogAnchorRect);
      return dlg.exec();
    }
    // The popover is a CHILD WIDGET, never a window of its own: a small frameless
    // top-level simply does not animate on macOS. As a child, grow/shrink are
    // ordinary widget animations; the dialog keeps its content/result identity.
    // This branch flies itself (below) — opt out of the app-wide DialogRevealFilter,
    // or its uninvited flight piles onto this same dlg with mismatched geometry/timing.
    dlg.setProperty(support::kNoDialogRevealProperty, true);
    const QSize cap(470, 590);
    dlg.setMinimumSize(0, 0);
    dlg.setMaximumSize(cap);
    const QSize want(qMin(dlg.sizeHint().width(), cap.width()),
                     qMin(dlg.sizeHint().height(), cap.height()));

    auto* overlay = new QWidget(this);
    overlay->setObjectName(QStringLiteral("popoverOverlay"));   // themed + found by tests
    // The logo's own popover extends the logo's hover: the shine holds while the
    // cursor is on the box (browser: the menu lives inside .app-logo-wrap).
    if (anchor == logoBtn_ && logoFx_) asLogoFx(logoFx_)->holdWhile(overlay);
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
    // Final geometry up front — grab() below needs the popover at its landed size.
    overlay->setGeometry(box);
    overlay->raise();
    overlay->show();
    dlg.setFocus(Qt::PopupFocusReason);   // Escape and typing go to the popover
    // Grow out of the icon via the shared particle dust; falls back to a plain
    // grow+fade box when the flight declines (reduced motion, an unmeasurable box).
    if (!support::motionReduced()) {
      const QPixmap shot = overlay->grab();
      gui::DisintegrateOverlay* dust =
          shot.isNull() ? nullptr
                        : gui::DisintegrateOverlay::overSurface(
                              shot, box, this, fromBox.center(), /*gather=*/true,
                              kPopoverOpenMs, overlay->palette().color(QPalette::WindowText),
                              support::kDialogDustMaxCells);
      auto* fx = new QGraphicsOpacityEffect(overlay);
      overlay->setGraphicsEffect(fx);
      fx->setOpacity(0.0);
      if (dust) {
        // Fades up as the last motes land, like every other window's open flight.
        auto* fade = new QPropertyAnimation(fx, "opacity", overlay);
        fade->setDuration(kPopoverOpenMs);
        fade->setKeyValueAt(0.0, 0.0);
        fade->setKeyValueAt(0.55, 0.0);
        fade->setKeyValueAt(1.0, 1.0);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
      } else {
        overlay->setGeometry(fromBox);
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
    }
    pop_.active = &dlg;
    pop_.overlay = overlay;
    // Alt-GLIDE: while any popover shows and Alt is HELD, the cursor landing on a
    // DIFFERENT popover icon closes this dialog and opens that icon's peek. The
    // modal loop blocks Enter/hover events, so a poll watches the cursor.
    QTimer glide;
    glide.setInterval(80);
    connect(&glide, &QTimer::timeout, this, [this, anchor] {
      if (!pop_.active) return;
      // altHeldForTest_: the offscreen GUI test's stand-in for a physically held Alt
      // (QTest key events never reach the platform's modifier state).
      if (!(QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier) && !altHeldForTest_)
        return;
      // Whether the cursor rests ON the open box — see the fallback below.
      const bool onBox = popoverRectGlobal().contains(QCursor::pos());
      for (auto it = pop_.buttons.cbegin(); it != pop_.buttons.cend(); ++it) {
        auto* b = static_cast<QToolButton*>(it.key());
        if (b == anchor || !b->isVisible() || !it.value()->isEnabled()) continue;
        // underMouse() as backup, same as the Alt KeyPress loop (and the test's mock).
        // The cursor-rect half is pure GEOMETRY and blind to what COVERS the icon, so
        // resting on the box read as resting on the icons under it. underMouse() has no
        // such problem (the overlay takes the hover), so only the fallback is guarded.
        const bool hovering = b->underMouse() ||
                              (!onBox && b->rect().contains(b->mapFromGlobal(QCursor::pos())));
        if (!hovering) continue;
        pop_.peekNextButton = b;
        pop_.peekNextAction = it.value();
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
      // grab() still renders a hidden widget — the dialog already hid on its way here.
      const QPixmap shot = alive ? alive->grab() : QPixmap();
      if (alive) {
        auto* frozen = new QLabel(overlayAlive);
        frozen->setPixmap(shot);
        frozen->setGeometry(alive->geometry());
        frozen->show();
      }
      // Pours back into the icon it grew from, same dust as the open flight.
      if (!shot.isNull() && gui::DisintegrateOverlay::overSurface(
                                shot, overlayAlive->geometry(), this, fromBox.center(),
                                /*gather=*/false, kPopoverCloseMs,
                                overlayAlive->palette().color(QPalette::WindowText),
                                support::kDialogDustMaxCells)) {
        overlayAlive->hide();
        return end();
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
    pop_.active.clear();
    pop_.overlay.clear();
    const int result = alive ? alive->result() : int(QDialog::Rejected);
    // Hand the dialog back to its caller — it is a stack object, so it must NOT be left
    // parented to the overlay we are about to delete.
    if (alive) {
      alive->hide();
      alive->setParent(nullptr);
    }
    if (overlayAlive) overlayAlive->deleteLater();
    // The glide picked the next icon: open its peek once this dialog unwinds.
    if (pop_.peekNextAction) {
      QTimer::singleShot(0, this, [this] {
        QToolButton* b = pop_.peekNextButton.data();
        QAction* a = pop_.peekNextAction.data();
        pop_.peekNextButton.clear();
        pop_.peekNextAction.clear();
        altPeekOpen(b, a);
      });
    }
    return result;
  }

  void MainWindow::openConnections() {
    ConnectDialog dlg(ensureConnections(), this);
    // "Sync changes to server" sits beside Auto-connect there (browser parity); the
    // setting stays ours, so a toggle runs the ordinary settings path.
    dlg.setSyncToServer(settings_.syncToServer);
    connect(&dlg, &ConnectDialog::syncToServerToggled, this, [this](bool on) {
      Settings s = settings_;
      s.syncToServer = on;
      applySettings(s, true);
    });
    // The dialog reports on the app's toast stack, exactly as the browser does — never a
    // native alert box in front of the window you are working in. It stays open behind
    // the toast, like the projects dialog's own messages.
    connect(&dlg, &ConnectDialog::toast, this, [this](const QString& text, bool failed) {
      if (!notify_) return;
      if (failed) notify_->error(text); else notify_->success(text);
    });
    execMaybePopover(dlg, actConnect_);
    warnInsecureConnections();  // the dialog may have added a plaintext-remote connection
  }

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
      // incognito editor's content (see the incognito scope note above).
      fileStore::saveProjects(projectList_);
    }

    ProjectsDialog dlg(projectList_, nowMs(), connections_, buildProjectThumbs(),
                       this, activeProjectId_, accentPrimary(settings_.accentColor));
    // No project open here (and no server session standing in for one): the list shows
    // this window as the pinned "Temporary (unsaved)" row, as the browser's does. Re-asked
    // with every removal below — deleting the OPEN project resets this window to a blank
    // unsaved editor (eraseLocalProject → resetToBlankEditor), so the pinned row must
    // appear then, exactly as the browser's list does; a stale `false` left the emptied
    // list reading "No projects yet" instead. It travels WITH the new
    // project list, in one repaint: answering it separately showed the batch bar for the
    // stale row and took it away a beat later, and the arriving row jumped with it.
    const auto unsavedSession = [this] {
      return activeProjectId_.isEmpty() && remoteSession_->link().id.isEmpty();
    };
    dlg.setTemporary(unsavedSession(), incognito_);
    dlg.setDragZones(projectZones_);   // the main-window drag-out zone overlay (open/new-window/remove)
    // "Clear All (Local)" is handled WHILE the dialog is up: it confirms itself (over its
    // own window), we remove the projects, and it repaints the now-empty list. Closing the
    // window to ask, then leaving it closed, lost the user their place.
    connect(&dlg, &ProjectsDialog::clearAllRequested, this, [this, &dlg, unsavedSession] {
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
      QTimer::singleShot(DisintegrateOverlay::kMs, this, [this, live, unsavedSession] {
        if (live) live->setProjects(projectList_, unsavedSession(), incognito_);
      });
      notify_->success(QString("Cleared %1 local project(s)").arg(n));
    });
    // Single Delete / batch Remove: same stay-open pattern — the dialog confirmed and is
    // scattering the rows; remove here, then repaint the still-open list once the dust lands.
    connect(&dlg, &ProjectsDialog::removeRequested, this,
            [this, &dlg, unsavedSession](const QVector<QPair<QString, QString>>& items) {
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
      // incognito editor's content (see the incognito scope note above).
      fileStore::saveProjects(projectList_);
      refreshActions();
      refreshDockMenu();  // drop it from the Dock "recent" list
      if (single) notify_->info("Project deleted");
      // Rebuild once the motes have landed (see the Clear All note above).
      QTimer::singleShot(DisintegrateOverlay::kMs, this, [this, live, unsavedSession] {
        if (live) live->setProjects(projectList_, unsavedSession(), incognito_);
      });
    });
    // Inline rename (dblclick on the row's name): same stay-open pattern — the dialog
    // already validated; rename here and repaint the still-open list.
    connect(&dlg, &ProjectsDialog::renameRequested, this,
            [this, &dlg](const QString& id, const QString& name) {
      renameProjectById(id, name);
      dlg.setProjects(projectList_);
    });
    // "Set expiration": the editor runs over the still-open list (browser parity), so
    // write the meta here and repaint. Not gated by incognito — it operates on other
    // saved projects, not the incognito editor's content (see the incognito scope note above).
    // Per-row "Open in another app" — the list stays up while the hand-off dialog runs.
    const QString botUser = settings_.telegramBotUsername.trimmed();
    const bool browserTarget = !settings_.browserBaseUrl.trimmed().isEmpty();
    dlg.setOpenInAvailable(browserTarget, browserTarget || !botUser.isEmpty());
    connect(&dlg, &ProjectsDialog::openInRequested, this,
            [this](const QString& id, const QString& serverUrl, const QRect& closeRect) {
      openInAnotherAppFor(id, serverUrl, closeRect);
    });
    connect(&dlg, &ProjectsDialog::expirationRequested, this,
            [this, &dlg](const QString& id, long long expiresAt, const QString& period,
                         bool autoRefresh) {
      Project* pr = findProject(id.toStdString());
      if (!pr) return;
      pr->meta.expiresAt = expiresAt;
      pr->meta.refreshPeriod = period.toStdString();
      pr->meta.autoRefresh = autoRefresh;
      fileStore::saveProjects(projectList_);
      dlg.setProjects(projectList_);
      const QString shown = support::shortName(QString::fromStdString(pr->meta.name));
      notify_->success(expiresAt == 0 ? QString("\"%1\" is kept forever").arg(shown)
                                      : QString("\"%1\" expiration updated").arg(shown));
    });
    if (execMaybePopover(dlg) != QDialog::Accepted) return;

    using Action = ProjectsDialog::Action;
    // No open-confirm here: the dialog asks its own "Open this project?" question
    // IN-DIALOG (ProjectsDialog::finishOpen), so an accepted Open is already confirmed.
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

}
