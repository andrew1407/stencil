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

  // App-lifetime macOS Dock menu, shared by all windows.
  QMenu* MainWindow::sDockMenu_ = nullptr;

  MainWindow::MainWindow(QWidget* parent, bool restoreLast)
      : QMainWindow(parent) {
    setWindowTitle("Stencil");
    resize(1100, 760);
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

    // App-wide and idempotent: a second window installs nothing new.
    installAppTooltips();

    wireSignals();

    restorePersistedState(restoreLast);
  }

  // Defined here so unique_ptr members of forward-declared types see their complete type.
  // QWidget deletes children BEFORE QObject drops connections: teardown slots must bail on this flag.
  MainWindow::~MainWindow() {
    tearingDown_ = true;
    setBlockedCursor(false);   // never leave the app-wide override pushed behind us
  }

  // Defaults + labels from the embedded config, user overrides on top (browser STORAGE_KEYS.hotkeys merge).
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
    // Alt+Shift+arrow chords fire from keyPressEvent (no QAction): defaults + labels only, so the shortcuts dialog lists them.
    struct ChordDef { const char* id; const char* seq; const char* label; };
    static const ChordDef LINE_TRANSFORM_CHORDS[] = {
        {"flipLineHorizontal", "Alt+Shift+Up", "Flip Selected Line Horizontal"},
        {"flipLineVertical", "Alt+Shift+Down", "Flip Selected Line Vertical"},
        {"rotateLineCW90", "Alt+Shift+Right", "Rotate Selected Line +90°"},
        {"rotateLineCCW90", "Alt+Shift+Left", "Rotate Selected Line −90°"},
    };
    for (const auto& c : LINE_TRANSFORM_CHORDS) {
      hotkeyDefaults_.insert(c.id, c.seq);
      hotkeyLabels_.insert(c.id, c.label);
      hotkeys_.insert(c.id, c.seq);
      hotkeyOrder_.append(c.id);
    }
    const auto overrides = fileStore::loadHotkeys();
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
      hotkeys_.insert(it.key(), it.value());
  }

  // The connect() ORDER is observable (allowFormulas_ drives actAllowFormulas_; customW/H call onSelectionChanged) — keep it verbatim.
  void MainWindow::wireSignals() {
    // Dock visibility → toggle sync + provider probe.
    connect(chatDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
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
    // Idle-canvas click grows the blank creator out of the CARD, not the toolbar icon.
    connect(canvas_, &CanvasWidget::blankImageRequested, this, [this] {
      const QRect card = canvas_ ? canvas_->idleCardGlobalRect() : QRect();
      if (card.isValid()) {
        pop_.dialogAnchor.clear();       // the rect below is the origin, not any icon
        pop_.dialogAnchorRect = card;
      }
      openImageDialog(/*startBlank=*/true);
    });
    connect(canvas_, &CanvasWidget::drawingModeChanged, this,
            &MainWindow::refreshActions);
    connect(canvas_, &CanvasWidget::hoverDetail, this,
            &MainWindow::onHoverDetail);
    connect(canvas_, &CanvasWidget::hoverLeft, this,
            [this] { hideHoverTooltip(); });
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
              // Step 0.1 (0.3 with Shift), additive — drawingApp.js wheel.
              const double step = fast ? 0.3 : 0.1;
              const double target = canvas_->scale() + dir * step;
              // posInWidget is canvas-space; the focal math wants viewport coords.
              const QPoint inVp =
                  canvas_->mapTo(scroll_->viewport(), posInWidget);
              setZoomAnchored(target, inVp);
            });
    connect(canvas_, &CanvasWidget::zoomByFactorAt, this,
            [this](double factor, const QPoint& posInWidget) {
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
      // syncCombo=false: don't re-write the field being read.
      QString s = t;
      s.remove('%');
      bool ok = false;
      const double pct = s.trimmed().toDouble(&ok);
      if (ok) setZoom(pct / 100.0, false);
    });
    // Index-based: the editable search field mutates the text per keystroke.
    connect(units_.pageSize, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onPageSizeChanged(); });
    connect(units_.customW, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
              // Edited in the active unit; the model stores cm.
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
    // The toolbar checkbox is the source of truth; the View action just drives it.
    connect(allowFormulas_, &QCheckBox::toggled, this, [this](bool on) {
      settings_.allowFormulas = on;
      revealControls(formulaGroup_, on);
      if (actAllowFormulas_ && actAllowFormulas_->isChecked() != on) {
        QSignalBlocker ba(actAllowFormulas_);
        actAllowFormulas_->setChecked(on);
      }
      // Expressions are KEPT while disabled so re-enabling restores them.
      if (!on) formulaError_->setVisible(false);
      persistSettings();
      onHovered(lastHoverX_, lastHoverY_);
      onSelectionChanged();  // refresh panel cm when formulas toggle (GAP-2)
      remoteSync_->scheduleRemotePush();  // formulas ride the layout — push to peers
    });
    connect(actAllowFormulas_, &QAction::toggled, this,
            [this](bool on) { allowFormulas_->setChecked(on); });
    // The f(x,y) pair commits on an idle pause (see the timer); Enter / focus-out apply at once.
    formulaCommitTimer_ = new QTimer(this);
    formulaCommitTimer_->setSingleShot(true);
    formulaCommitTimer_->setInterval(FORMULA_COMMIT_MS);
    connect(formulaCommitTimer_, &QTimer::timeout, this, [this] { validateAndApplyFormulas(); });
    const auto onFormulaEdited = [this](const QString&) {
      // A wrong expression is only flagged once typing stops.
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

    // Hover cross-highlight, both directions (browser parity); never scrolls a list.
    connect(selPanel_, &SelectionPanel::pointRowHovered, this,
            [this](int i) { canvas_->setListHoverPoint(i); });
    connect(selPanel_, &SelectionPanel::lineRowHovered, this,
            [this](int i) { canvas_->setListHoverLine(i); });
    connect(canvas_, &CanvasWidget::canvasHoverChanged, this,
            [this](int lineIdx, int ptIdx, int overLineIdx) {
              const bool onPanelLine = ptIdx >= 0 && lineIdx == canvas_->panelLineIdx();
              selPanel_->setCanvasHover(onPanelLine ? ptIdx : -1, overLineIdx);
            });

    // "Selected Line:" bar → canvas mutators — drawingApp.js:181-195; no Delete here (browser parity).
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
    connect(canvas_, &CanvasWidget::statusMessage, this,
            [this](const QString& text) { notify_->success(text); });
    // Routes through actPanel_ so the View menu / Alt+X stay in sync.
    connect(selPanel_, &SelectionPanel::collapseRequested, this,
            [this] { if (actPanel_) actPanel_->setChecked(false); });
    selPanel_->setToggleHint(hotkey("togglePointsList", "Alt+X"));   // shortcut in the chevron tooltip

    // Lines tab → index-keyed selection (Ctrl/⌘+Shift toggles multi-select) and removal.
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

  // Live-sync the submenu state before exec — contextMenu.js:239-297 syncState().
  void MainWindow::syncContextActions() {
    const bool hasImg = canvas_->hasImage();
    const bool hasLines = !canvas_->allLines().empty();
    actFullscreen_->setText(isFullScreen() ? QStringLiteral("Exit Fullscreen")
                                           : QStringLiteral("Enter Fullscreen"));
    setActionTip(actFullscreen_, isFullScreen() ? "Exit fullscreen" : "Fullscreen mode");

    // contextMenu.js:254-264
    syncExportActions();
    actPasteImage_->setEnabled(true);  // dispatch notifies "Load an image first"
    actCopyLayout_->setEnabled(hasLines);
    actDownloadJson_->setEnabled(hasLines);
    actPasteLayout_->setEnabled(hasImg);
    actUploadJson_->setEnabled(hasImg);
    actSaveProjectFile_->setEnabled(hasImg);

    // contextMenu.js:274-276; blocked so seeding doesn't re-fire handlers.
    {
      QSignalBlocker bm(pointSpin_), bt(thickSpin_);
      pointSpin_->setValue(settings_.defaultPointSize);
      thickSpin_->setValue(settings_.defaultThickness);
    }
    for (QAction* a : lineStyleGroup_->actions())
      a->setChecked(a->data().toString() == settings_.defaultStyle);

    // contextMenu.js:278-282
    for (QAbstractButton* b : filterButtons_->buttons()) {
      QSignalBlocker bl(b);   // seeding the check state must not re-fire applyImageFilter
      b->setChecked(b->property("filterValue").toString() == settings_.imageFilter);
    }
    tintColorAction_->setVisible(settings_.imageFilter == "custom");

    // contextMenu.js:289-293; blocked so seeding doesn't re-fire the toggle handlers.
    {
      QSignalBlocker be(tooltipEnableCheck_), bp(ttPageCheck_), bs(ttScreenCheck_), bc(ttCoordsCheck_);
      tooltipEnableCheck_->setChecked(settings_.tooltipEnabled);
      ttPageCheck_->setChecked(settings_.tooltipShowPage);
      ttScreenCheck_->setChecked(settings_.tooltipShowScreen);
      ttCoordsCheck_->setChecked(settings_.tooltipShowCoords);
    }

    // contextMenu.js:294-297; blocked so seeding doesn't re-apply.
    {
      QSignalBlocker ba(ctxAllowFormulas_), bx(ctxFormulaX_), by(ctxFormulaY_);
      ctxAllowFormulas_->setChecked(settings_.allowFormulas);
      ctxFormulaX_->setText(settings_.formulaX);
      ctxFormulaY_->setText(settings_.formulaY);
    }
    ctxFormulaXAct_->setVisible(settings_.allowFormulas);
    ctxFormulaYAct_->setVisible(settings_.allowFormulas);
  }

  // Layout/image export + clipboard IO live in DataExportController; pasteImage() stays here (it creates a project).

  // Ctrl+V: an image on the clipboard wins, else layout JSON text — drawingApp.js :563-591.
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
    dataExport_->pasteLayout();
  }

  // A modal dialog runs its own event loop, so the main window's QActions never fire there: the dialog carries copies
  // of those chords while showing and the originals are parked (a live twin with the same chord would be ambiguous).
  template <typename Actions>
  static void wireWindowSwitching(QDialog& dlg, const Actions& actions, QAction* opener) {
    for (QAction* a : actions) {
      if (!a || a->shortcut().isEmpty()) continue;
      auto* sc = new QShortcut(a->shortcut(), &dlg);
      sc->setContext(Qt::WidgetWithChildrenShortcut);
      QObject::connect(sc, &QShortcut::activated, &dlg, [&dlg, a, opener] {
        // A different window's chord: close, then open that one once this dialog's loop has unwound.
        if (a != opener) QTimer::singleShot(0, a, &QAction::trigger);
        dlg.reject();
      });
    }
  }

  // llm-contract.md §5; browser twin llmSettingsModal.js.
  void MainWindow::openAssistantSettings() {
    // The gear sits inside the dock's "…" menu, already closed — the flight belongs to the "…" trigger.
    openAssistantSettingsFrom(chatDock_ ? chatDock_->moreButton() : nullptr);
  }

  void MainWindow::openAssistantSettingsFrom(QWidget* anchor, const QRect& anchorRect) {
    AssistantSettingsDialog dlg(settings_, this);
    wireWindowSwitching(dlg, pop_.dialogActions, actAssistantSettings_);
    support::revealDialog(dlg, anchor, anchorRect);
    if (dlg.exec() == QDialog::Accepted) {
      applySettings(dlg.result(), true);
    }
  }

  void MainWindow::openSettings() {
    SettingsDialog dlg(settings_, this);
    // Live-apply: every row persists itself as it changes; no Save/Cancel.
    dlg.setOnChange([this](const Settings& s) { applySettings(s, true); });
    connect(&dlg, &SettingsDialog::visualsReset, this,
            [this] { notify_->success(QStringLiteral("Visual defaults reset")); });
    execMaybePopover(dlg, actSettings_);
    // Settle-up catches a field left mid-edit; skipped when nothing changed.
    if (fileStore::settingsToJson(dlg.result()) != fileStore::settingsToJson(settings_))
      applySettings(dlg.result(), true);
  }

  // The popover's motion: the dialog reveal (modalReveal.cpp) ×1.5.
  static constexpr int POPOVER_OPEN_MS = 450;
  static constexpr int POPOVER_CLOSE_MS = 360;

  int MainWindow::execMaybePopover(QDialog& dlg, QAction* opener) {
    wireWindowSwitching(dlg, pop_.dialogActions, opener);
    // Park the originals while the dialog owns those chords.
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
      support::revealDialog(dlg, pop_.dialogAnchor.data(), pop_.dialogAnchorRect);
      return dlg.exec();
    }
    // A CHILD WIDGET, never its own window: a small frameless top-level does not animate on macOS.
    // This branch flies itself — opt out of the app-wide DialogRevealFilter or its flight piles on.
    dlg.setProperty(support::NO_DIALOG_REVEAL_PROPERTY, true);
    const QSize cap(470, 590);
    dlg.setMinimumSize(0, 0);
    dlg.setMaximumSize(cap);
    const QSize want(qMin(dlg.sizeHint().width(), cap.width()),
                     qMin(dlg.sizeHint().height(), cap.height()));

    auto* overlay = new QWidget(this);
    overlay->setObjectName(QStringLiteral("popoverOverlay"));   // themed + found by tests
    // The popover extends the logo's hover (browser: the menu lives inside .app-logo-wrap).
    if (anchor == logoBtn_ && logoFx_) asLogoFx(logoFx_)->holdWhile(overlay);
    overlay->setAutoFillBackground(true);
    dlg.setParent(overlay);
    dlg.setWindowFlags(Qt::Widget);   // a plain child now: no frame, no title, no window
    dlg.setGeometry(QRect(QPoint(0, 0), want));
    dlg.show();

    // WINDOW coordinates, kept inside the window.
    const QRect anchorGlobal(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const QRect windowGlobal(mapToGlobal(QPoint(0, 0)), size());
    const QRect box(mapFromGlobal(support::popoverRect(anchorGlobal, want, windowGlobal)
                                      .topLeft()),
                    want);
    const QRect fromBox(mapFromGlobal(anchorGlobal.topLeft()), anchorGlobal.size());
    // Final geometry first — grab() below needs the landed size.
    overlay->setGeometry(box);
    overlay->raise();
    overlay->show();
    dlg.setFocus(Qt::PopupFocusReason);   // Escape and typing go to the popover
    // Falls back to a plain grow+fade when the flight declines.
    if (!support::motionReduced()) {
      const QPixmap shot = overlay->grab();
      gui::DisintegrateOverlay* dust =
          shot.isNull() ? nullptr
                        : gui::DisintegrateOverlay::overSurface(
                              shot, box, this, fromBox.center(), /*gather=*/true,
                              POPOVER_OPEN_MS, overlay->palette().color(QPalette::WindowText),
                              support::DIALOG_DUST_MAX_CELLS);
      auto* fx = new QGraphicsOpacityEffect(overlay);
      overlay->setGraphicsEffect(fx);
      fx->setOpacity(0.0);
      if (dust) {
        auto* fade = new QPropertyAnimation(fx, "opacity", overlay);
        fade->setDuration(POPOVER_OPEN_MS);
        fade->setKeyValueAt(0.0, 0.0);
        fade->setKeyValueAt(0.55, 0.0);
        fade->setKeyValueAt(1.0, 1.0);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
      } else {
        overlay->setGeometry(fromBox);
        auto* grow = new QPropertyAnimation(overlay, "geometry", overlay);
        grow->setDuration(POPOVER_OPEN_MS);
        grow->setStartValue(fromBox);
        grow->setEndValue(box);
        grow->setEasingCurve(QEasingCurve::OutCubic);
        auto* fade = new QPropertyAnimation(fx, "opacity", overlay);
        fade->setDuration(POPOVER_OPEN_MS);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        grow->start(QAbstractAnimation::DeleteWhenStopped);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
      }
    }
    pop_.active = &dlg;
    pop_.overlay = overlay;
    // Alt-GLIDE: the modal loop blocks Enter/hover events, so a poll watches the cursor.
    QTimer glide;
    glide.setInterval(80);
    connect(&glide, &QTimer::timeout, this, [this, anchor] {
      if (!pop_.active) return;
      // altHeldForTest_: the offscreen GUI test's stand-in for a held Alt (QTest never sets platform modifier state).
      if (!(QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier) && !altHeldForTest_)
        return;
      const bool onBox = popoverRectGlobal().contains(QCursor::pos());
      for (auto it = pop_.buttons.cbegin(); it != pop_.buttons.cend(); ++it) {
        auto* b = static_cast<QToolButton*>(it.key());
        if (b == anchor || !b->isVisible() || !it.value()->isEnabled()) continue;
        // The cursor-rect half is blind to what COVERS the icon; underMouse() sees the overlay, so only the fallback is guarded.
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
    // A nested loop, not exec(): the caller still blocks and reads a DialogCode, but there is no modal window at all.
    QPointer<QDialog> alive(&dlg);
    QPointer<QWidget> overlayAlive(overlay);
    QEventLoop loop;
    bool ended = false, closing = false;
    const auto end = [&ended, &loop] { ended = true; loop.quit(); };
    // ONE close path for every ending: freeze the picture into the overlay, then collapse it back into the icon.
    connect(&dlg, &QDialog::finished, &loop, [&] {
      if (closing) return;   // a second reject during the collapse is a no-op
      closing = true;
      if (!overlayAlive || support::motionReduced()) return end();
      const QPixmap shot = alive ? alive->grab() : QPixmap();
      if (alive) {
        auto* frozen = new QLabel(overlayAlive);
        frozen->setPixmap(shot);
        frozen->setGeometry(alive->geometry());
        frozen->show();
      }
      if (!shot.isNull() && gui::DisintegrateOverlay::overSurface(
                                shot, overlayAlive->geometry(), this, fromBox.center(),
                                /*gather=*/false, POPOVER_CLOSE_MS,
                                overlayAlive->palette().color(QPalette::WindowText),
                                support::DIALOG_DUST_MAX_CELLS)) {
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
      shrink->setDuration(POPOVER_CLOSE_MS);
      shrink->setStartValue(overlayAlive->geometry());
      shrink->setEndValue(fromBox);
      shrink->setEasingCurve(QEasingCurve::InCubic);
      auto* fade = new QPropertyAnimation(fx, "opacity", overlayAlive);
      fade->setDuration(POPOVER_CLOSE_MS);
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
    // The dialog is a stack object — never leave it parented to the overlay about to be deleted.
    if (alive) {
      alive->hide();
      alive->setParent(nullptr);
    }
    if (overlayAlive) overlayAlive->deleteLater();
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
    // Sits beside Auto-connect there (browser parity).
    dlg.setSyncToServer(settings_.syncToServer);
    connect(&dlg, &ConnectDialog::syncToServerToggled, this, [this](bool on) {
      Settings s = settings_;
      s.syncToServer = on;
      applySettings(s, true);
    });
    // Reports on the toast stack, never a native alert (browser parity).
    connect(&dlg, &ConnectDialog::toast, this, [this](const QString& text, bool failed) {
      if (!notify_) return;
      if (failed) notify_->error(text); else notify_->success(text);
    });
    execMaybePopover(dlg, actConnect_);
    warnInsecureConnections();  // the dialog may have added a plaintext-remote connection
  }

  void MainWindow::openProjects() {
    // Expiry sweep (one week).
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
      // Not gated by incognito: other saved projects, not the incognito editor's content.
      fileStore::saveProjects(projectList_);
    }

    ProjectsDialog dlg(projectList_, nowMs(), connections_, buildProjectThumbs(),
                       this, activeProjectId_, accentPrimary(settings_.accentColor));
    // No project open: the list pins this window as "Temporary (unsaved)". Re-asked per removal — deleting the OPEN project
    // resets this window to a blank editor — and it travels WITH the list so the batch bar and row repaint together.
    const auto unsavedSession = [this] {
      return activeProjectId_.isEmpty() && remoteSession_->link().id.isEmpty();
    };
    dlg.setTemporary(unsavedSession(), incognito_);
    dlg.setDragZones(projectZones_);   // the main-window drag-out zone overlay (open/new-window/remove)
    // Handled WHILE the dialog is up: it confirms itself, we remove, it repaints.
    connect(&dlg, &ProjectsDialog::clearAllRequested, this, [this, &dlg, unsavedSession] {
      const int n = static_cast<int>(projectList_.size());
      const bool hadActive = !activeProjectId_.isEmpty();
      projectList_.clear();
      if (hadActive) resetToBlankEditor();   // the open one went with them
      fileStore::saveProjects(projectList_);
      refreshActions();
      refreshDockMenu();
      // Rows are still scattering; rebuild once the motes have landed (browser: beginRemoval).
      QPointer<ProjectsDialog> live(&dlg);
      QTimer::singleShot(DisintegrateOverlay::DUST_MS, this, [this, live, unsavedSession] {
        if (live) live->setProjects(projectList_, unsavedSession(), incognito_);
      });
      notify_->success(QString("Cleared %1 local project(s)").arg(n));
    });
    // Same stay-open pattern: remove, then repaint once the dust lands.
    connect(&dlg, &ProjectsDialog::removeRequested, this,
            [this, &dlg, unsavedSession](const QVector<QPair<QString, QString>>& items) {
      QPointer<ProjectsDialog> live(&dlg);
      const bool single = items.size() == 1 && items.first().second.isEmpty();
      // A project open in another window cannot be removed (browser "open in another tab" guard).
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
      fileStore::saveProjects(projectList_);
      refreshActions();
      refreshDockMenu();  // drop it from the Dock "recent" list
      if (single) notify_->info("Project deleted");
      QTimer::singleShot(DisintegrateOverlay::DUST_MS, this, [this, live, unsavedSession] {
        if (live) live->setProjects(projectList_, unsavedSession(), incognito_);
      });
    });
    // Inline rename: same stay-open pattern.
    connect(&dlg, &ProjectsDialog::renameRequested, this,
            [this, &dlg](const QString& id, const QString& name) {
      renameProjectById(id, name);
      dlg.setProjects(projectList_);
    });
    // "Set expiration" / "Open in another app": the list stays up.
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

    typedef ProjectsDialog::Action Action;
    // Open is already confirmed IN-DIALOG (ProjectsDialog::finishOpen).
    if (dlg.action() == Action::OPEN) {
      loadProjectIntoCanvas(dlg.selectedId());
    } else if (dlg.action() == Action::OPEN_REMOTE) {
      openServerProject(dlg.selectedServerUrl(), dlg.selectedId());
    } else if (dlg.action() == Action::OPEN_IN_NEW_WINDOW) {
      openProjectInNewWindow(dlg.selectedId());
    } else if (dlg.action() == Action::MOVE_TO_SERVER) {
      if (projectOpenInOtherWindow(dlg.selectedId())) {
        notify_->error("That project is open in another window — close it there first");
        return;
      }
      projectTransfer_->moveLocalProjectToServer(dlg.selectedServerUrl(), dlg.selectedId());
    } else if (dlg.action() == Action::COPY_TO_SERVER) {
      projectTransfer_->copyLocalProjectToServer(dlg.selectedServerUrl(), dlg.selectedId(), dlg.newName());
    } else if (dlg.action() == Action::MOVE_TO_LOCAL) {
      // Move-to-local is allowed with a peer open: the server delete just ends their live link.
      projectTransfer_->moveServerProjectToLocal(dlg.selectedServerUrl(), dlg.selectedId());
    } else if (dlg.action() == Action::MAKE_LOCAL_COPY) {
      projectTransfer_->makeLocalCopyOfServerProject(dlg.selectedServerUrl(), dlg.selectedId(), dlg.newName());
    } else if (dlg.action() == Action::BATCH_MOVE_TO_SERVER) {
      for (const auto& pr : dlg.batchItems()) projectTransfer_->moveLocalProjectToServer(dlg.selectedServerUrl(), pr.first);
    } else if (dlg.action() == Action::BATCH_COPY_TO_SERVER) {
      for (const auto& pr : dlg.batchItems()) projectTransfer_->copyLocalProjectToServer(dlg.selectedServerUrl(), pr.first, QString());
    } else if (dlg.action() == Action::BATCH_MOVE_TO_LOCAL) {
      for (const auto& pr : dlg.batchItems()) projectTransfer_->moveServerProjectToLocal(pr.second, pr.first);
    } else if (dlg.action() == Action::BATCH_COPY_TO_LOCAL) {
      // Each import is async; refresh + notify once the last one lands.
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
    } else if (dlg.action() == Action::SET_COLOR) {
      // Capture the selection by value — `dlg` dies when openProjects returns, before the async PUT completes.
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
    } else if (dlg.action() == Action::RENAME) {
      renameProjectById(dlg.selectedId(), dlg.newName());
    } else if (dlg.action() == Action::NEW) {
      if (incognito_) {  // an explicit promotion out of incognito, not an app-side write
        const QString promoted = promoteIncognitoToLocal(dlg.newName());
        notify_->success(promoted.isEmpty()
                             ? QStringLiteral("Nothing to save yet")
                             : QStringLiteral("Left incognito — saved \"%1\"")
                                   .arg(support::shortName(promoted)));
        return;
      }
      createProject(dlg.newName());
    } else if (dlg.action() == Action::NEW_BLANK) {
      newBlankImage();
    }
  }

}
