#include "mainWindow.hpp"
#include "blankImageDialog.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "core/cropGeometry.hpp"
#include "core/geometry.hpp"
#include "core/pageMetrics.hpp"
#include "cropDialog.hpp"
#include "core/tooltipRows.hpp"
#include "core/zoomPan.hpp"
#include "guiHelpers.hpp"
#include "infoDialog.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "selectionPanel.hpp"
#include "settingsDialog.hpp"
#include "shortcutsDialog.hpp"
#include "theme.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QPixmap>
#include <QStyleHints>
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
  }  // namespace

  MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Stencil (Qt)");
    resize(1100, 760);

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
    tooltip_ = new CanvasTooltip(this);  // floating hover tooltip (S12)

    status_ = new QLabel("Open an image — or create a blank one — to begin", this);
    statusBar()->addWidget(status_);

    pageSize_ = new QComboBox(this);
    pageSize_->addItems({"A3", "A4", "custom"});  // S10 custom page
    zoom_ = new QComboBox(this);
    zoom_->addItems({"25%", "50%", "75%", "100%", "150%", "200%", "400%"});
    // Editable so the user can type an exact percent, but NoInsert so reflecting
    // a programmatic zoom (Ctrl+wheel) never appends list items — mirrors browser
    // zoomPan.js setZoom (a clamped numeric percent, never an accumulating list).
    zoom_->setEditable(true);
    zoom_->setInsertPolicy(QComboBox::NoInsert);
    zoom_->setCurrentText("100%");

    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setSingleShot(true);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::saveSessionNow);

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
    restoreSession();
    refreshActions();
    onSelectionChanged();
    updateStatusIdle();

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
              if (!incognito_) fileStore::saveSettings(settings_);
              onHovered(lastHoverX_, lastHoverY_);
              onSelectionChanged();  // refresh panel cm (S10/GAP-2)
            });
    connect(customH_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
              settings_.customPageHeight = v / unitFormat().factor;
              if (!incognito_) fileStore::saveSettings(settings_);
              onHovered(lastHoverX_, lastHoverY_);
              onSelectionChanged();  // refresh panel cm (S10/GAP-2)
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
      if (!on) {  // browser clears the expressions when disabling
        settings_.formulaX.clear();
        settings_.formulaY.clear();
        formulaX_->clear();
        formulaY_->clear();
        formulaError_->setVisible(false);
      }
      if (!incognito_) fileStore::saveSettings(settings_);
      onHovered(lastHoverX_, lastHoverY_);
      onSelectionChanged();  // refresh panel cm when formulas toggle (GAP-2)
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

    actOpen_ = mk("Open Image…", "Ctrl+O");
    actNewBlank_ = mk("🖼 New Blank Image…", QString());
    actNewBlank_->setToolTip(
        "Create a blank image (white, black, or any color) to draw on");
    actCrop_ = mk("✂ Crop Image…", QString());
    actCrop_->setToolTip(
        "Crop the image — pick the page-shaped region to show on the canvas");
    // Non-destructive 90° rotation (browser hotkeys rotateImageLeft=Alt+R,
    // rotateImageRight=Alt+Shift+R). The crop window and lines follow the picture.
    actRotateLeft_ = mk("↺ Rotate Left", hotkey("rotateImageLeft", "Alt+R"));
    actRotateLeft_->setToolTip("Rotate the image left (counter-clockwise)");
    actRotateRight_ = mk("↻ Rotate Right", hotkey("rotateImageRight", "Alt+Shift+R"));
    actRotateRight_->setToolTip("Rotate the image right (clockwise)");
    // Start/Stop drawing (S5): mirrors hotkeysConfig startDraw=Alt+A,
    // stopDraw=Alt+S. actNewLine_ keeps "commit + begin a fresh line" but loses
    // its shortcut to avoid colliding with Stop (Alt+S now drives stopDraw).
    actStartDraw_ = mk("▶ Start Drawing", hotkey("startDraw", "Alt+A"));
    actStopDraw_ = mk("■ Stop Drawing", hotkey("stopDraw", "Alt+S"));
    actNewLine_ = mk("New Line", QString());
    actUndo_ = mk("Undo", hotkey("undo", "Ctrl+Z"));
    actRedo_ = mk("Redo", hotkey("redo", "Ctrl+Shift+Z"));
    actDeleteLast_ = mk("Delete Last Point", "Backspace");
    actClearAll_ = mk("Clear All Lines", hotkey("clearAllLines", "Alt+W"));
    actDeselect_ = mk("Deselect", "Esc");
    actZoomIn_ = mk("Zoom In", hotkey("zoomIn", "Alt+Up"));
    actZoomOut_ = mk("Zoom Out", hotkey("zoomOut", "Alt+Down"));
    actFit_ = mk("Fit to Window", hotkey("resetZoom", "Alt+0"));
    actShowPoints_ = mk("Show Points", hotkey("togglePoints", "Alt+P"));
    actShowLines_ = mk("Show Lines", hotkey("toggleLines", "Alt+L"));
    actTheme_ = mk("Dark Theme", "Ctrl+D");
    actPanel_ = mk("Selection Panel", hotkey("togglePointsList", "Alt+X"));
    actFullscreen_ = mk("Fullscreen", hotkey("fullscreen", "Alt+F"));
    actSettings_ = mk("Settings…", "Ctrl+,");
    actProjects_ = mk("Projects…", "Ctrl+Shift+P");
    actNewProject_ = mk("New Project", QString());
    actSaveProject_ = mk("Save to Project", "Ctrl+Shift+S");
    actSaveSession_ = mk("Save Session", "Ctrl+S");
    actInfo_ = mk("Info && Shortcuts", "F1");
    actIncognito_ = mk("🕶 Incognito", QString());
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
    actDownloadJson_ = mk("Export Layout JSON…", QString());
    actUploadJson_ = mk("Import Layout JSON…", QString());
    actCopyLayout_ = mk("Copy Layout JSON", hotkey("copyLayout", "Alt+J"));
    actPasteLayout_ = mk("Paste Layout JSON", QString());
    actSaveImage_ = mk("Save Image…", QString());
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
      notify_->success(clockwise ? "Rotated right" : "Rotated left");
    };
    connect(actRotateLeft_, &QAction::triggered, this, [rotate] { rotate(false); });
    connect(actRotateRight_, &QAction::triggered, this, [rotate] { rotate(true); });
    connect(actStartDraw_, &QAction::triggered, canvas_,
            &CanvasWidget::startDrawingMode);
    connect(actStopDraw_, &QAction::triggered, canvas_,
            &CanvasWidget::stopDrawingMode);
    connect(actNewLine_, &QAction::triggered, canvas_, &CanvasWidget::startNewLine);
    connect(actUndo_, &QAction::triggered, canvas_, &CanvasWidget::undo);
    connect(actRedo_, &QAction::triggered, canvas_, &CanvasWidget::redo);
    connect(actDeleteLast_, &QAction::triggered, canvas_,
            &CanvasWidget::deleteLastPoint);
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

    connect(actInfo_, &QAction::triggered, this, &MainWindow::openInfo);
    connect(actIncognito_, &QAction::toggled, this, [this](bool on) {
      incognito_ = on;
      notify_->info(on ? "Incognito mode — this editor won't be saved"
                       : "Incognito off");
    });
    connect(actTooltip_, &QAction::toggled, this, [this](bool on) {
      settings_.tooltipEnabled = on;
      if (!on) tooltip_->hide();
      if (!incognito_) fileStore::saveSettings(settings_);
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
      if (!incognito_) fileStore::saveSettings(settings_);
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
          QColorDialog::getColor(filterColorValue_, this, "Tint color");
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
    project->addAction(actNewProject_);
    project->addAction(actSaveProject_);

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
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);

    tb->addAction(actOpen_);
    tb->addAction(actNewBlank_);
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
      // Width-tightening (S8 req 7): keep the custom-page spinboxes compact
      // (browser style width:96px, toolbar.js:110/112).
      customW_->setMaximumWidth(96);
      customW_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
      customH_ = new QDoubleSpinBox(customGroup_);
      customH_->setRange(0.1, 500.0);
      customH_->setSingleStep(0.1);
      customH_->setDecimals(1);
      customH_->setValue(29.7);
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
    tb2->addWidget(allowFormulas_);
    formulaGroup_ = new QWidget(this);
    {
      auto* fl = new QHBoxLayout(formulaGroup_);
      fl->setContentsMargins(4, 0, 0, 0);
      fl->setSpacing(2);
      formulaX_ = new QLineEdit(formulaGroup_);
      formulaX_->setPlaceholderText("x(x)=");
      // Width-tightening (S8 req 7): compact f(x,y) inputs (browser width:90px,
      // toolbar.js:119/120) with a Fixed policy so they don't stretch.
      formulaX_->setMaximumWidth(90);
      formulaX_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
      formulaY_ = new QLineEdit(formulaGroup_);
      formulaY_->setPlaceholderText("y(y)=");
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
    drawModeBtn_->setText("╱ Line");
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
      if (!incognito_) fileStore::saveSettings(settings_);
    });
    // Echo the canvas draw mode onto the toggle button (drawingApp.js
    // syncDrawModeUI ~1125): label + tooltip per mode.
    connect(canvas_, &CanvasWidget::drawModeChanged, this,
            [this](CanvasWidget::DrawMode mode) {
              const bool rect = mode == CanvasWidget::DrawMode::Rect;
              drawModeBtn_->setText(rect ? "▭ Rect" : "╱ Line");
              drawModeBtn_->setToolTip(
                  rect ? "Drawing mode: Rectangle (click to switch to Line)"
                       : "Drawing mode: Line (click to switch to Rectangle)");
            });

    // Default line color (drawingApp.js:155): pick a color, store as the default
    // and push to the canvas.
    connect(lineColorBtn_, &QToolButton::clicked, this, [this] {
      const QColor c = QColorDialog::getColor(lineColorValue_, this,
                                              "Line color");
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
      const QColor c = QColorDialog::getColor(filterColorValue_, this,
                                              "Tint color");
      if (c.isValid()) applyTintColor(c);
    });
  }

  // Push the current default visuals to the canvas and persist (S8). Mirrors the
  // browser change handlers that update this.color/thickness/markerSize/style then
  // storage.save() (drawingApp.js:155-178). Defaults ONLY — never the selection.
  void MainWindow::onLineStyleControlChanged() {
    canvas_->setDefaults(settings_.defaultColor, settings_.defaultThickness,
                         settings_.defaultMarkerSize, settings_.defaultStyle);
    if (!incognito_) fileStore::saveSettings(settings_);
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
    if (!incognito_) fileStore::saveSettings(settings_);
  }

  void MainWindow::applyTintColor(const QColor& color) {
    filterColorValue_ = color;
    settings_.filterColor = color.name(QColor::HexRgb);
    if (filterColorBtn_) updateColorSwatch(filterColorBtn_, color);
    canvas_->setImageFilter(settings_.imageFilter, filterColorValue_);
    if (!incognito_) fileStore::saveSettings(settings_);
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

  void MainWindow::openImage() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open image", QString(), "Images (*.png *.jpg *.jpeg *.bmp *.gif)");
    if (path.isEmpty()) return;
    {
      const core::PageSize page = naturalPageCm(pageSize_->currentText(),
                                                settings_.customPageWidth,
                                                settings_.customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    if (!canvas_->loadImage(path)) {
      notify_->error("Failed to load image");
      return;
    }
    notify_->success("Image loaded");
    refreshActions();
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
    if (!incognito_) fileStore::saveSettings(settings_);
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
    if (!incognito_) fileStore::saveSettings(settings_);
    onHovered(lastHoverX_, lastHoverY_);
    onSelectionChanged();  // refresh panel cm for the new page size (GAP-2)
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
      if (!incognito_) fileStore::saveSettings(settings_);
      onHovered(lastHoverX_, lastHoverY_);
      onSelectionChanged();  // refresh panel cm with the new formulas (GAP-2)
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
  }

  void MainWindow::onCanvasChanged() {
    refreshActions();
    onSelectionChanged();
    scheduleAutosave();
  }

  void MainWindow::onSelectionChanged() {
    const core::Line* line = canvas_->panelLine();
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
    selPanel_->showLine(line, canvas_->selectedLine(), canvas_->selectedPoint(),
                        cmRows);
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
    const QString suggested = canvas_->imageBaseName() + "-layout.json";
    const QString path = QFileDialog::getSaveFileName(
        this, "Export layout JSON", suggested, "JSON (*.json)");
    if (path.isEmpty()) return;
    const QJsonObject obj = fileStore::buildLayoutJson(
        canvas_->imageWidth(), canvas_->imageHeight(), canvas_->allLines());
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
    const QJsonObject obj = fileStore::buildLayoutJson(
        canvas_->imageWidth(), canvas_->imageHeight(), canvas_->allLines());
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
    const QString suggested = canvas_->imageBaseName() + "-drawing." +
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
    int dx = 0, dy = 0;
    switch (event->key()) {
      case Qt::Key_Left:  dx = -step; break;
      case Qt::Key_Right: dx =  step; break;
      case Qt::Key_Up:    dy = -step; break;
      case Qt::Key_Down:  dy =  step; break;
      default: QMainWindow::keyPressEvent(event); return;
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
    qApp->setPalette(buildQPalette(dark));
    qApp->setStyleSheet(buildStylesheet(dark));
    canvas_->setDark(dark);
    actTheme_->setText(dark ? "Light Theme" : "Dark Theme");

    QPalette vp;
    vp.setColor(QPalette::Window, themePalette(dark).bgPage);
    scroll_->viewport()->setAutoFillBackground(true);
    scroll_->viewport()->setPalette(vp);
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

    ProjectsDialog dlg(projectList_, nowMs(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    using Action = ProjectsDialog::Action;
    if (dlg.action() == Action::Open) {
      loadProjectIntoCanvas(dlg.selectedId());
    } else if (dlg.action() == Action::OpenInNewWindow) {
      openProjectInNewWindow(dlg.selectedId());
    } else if (dlg.action() == Action::Delete) {
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
      notify_->info("Project deleted");
    } else if (dlg.action() == Action::Renew) {
      const std::string id = dlg.selectedId().toStdString();
      auto it = std::find_if(projectList_.begin(), projectList_.end(),
                             [&](const Project& p) { return p.meta.id == id; });
      if (it == projectList_.end()) return;
      // Restart the 7-day expiry window from now without touching content.
      it->meta.updatedAt = nowMs();
      // Not gated by incognito: operates on other saved projects, not the
      // incognito editor's content (see S6 scope note above).
      fileStore::saveProjects(projectList_);
      notify_->success(QString("Renewed \"%1\" — expires in 7 days")
                           .arg(QString::fromStdString(it->meta.name)));
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

  bool MainWindow::loadProjectIntoCanvas(const QString& id) {
    const std::string sid = id.toStdString();
    auto it = std::find_if(projectList_.begin(), projectList_.end(),
                           [&](const Project& p) { return p.meta.id == sid; });
    if (it == projectList_.end()) return false;
    {
      const core::PageSize page = naturalPageCm(pageSize_->currentText(),
                                                settings_.customPageWidth,
                                                settings_.customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    canvas_->restore(it->imagePath, it->lines, canvas_->scale(), it->cropRect,
                     it->rotationQuarters);
    activeProjectId_ = id;
    refreshActions();
    notify_->success(
        QString("Opened \"%1\"").arg(QString::fromStdString(it->meta.name)));
    return true;
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

  void MainWindow::newProjectFromCanvas() {
    if (incognito_) {  // S6: project promotion is blocked while incognito
      notify_->info("Incognito mode — saving is disabled");
      return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Project",
                                               "Project name:", QLineEdit::Normal,
                                               "Untitled", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    createProject(name.trimmed());
  }

  // Build a Project from the current canvas, persist it, mark it active, refresh,
  // and notify. Shared by openProjects' New action + newProjectFromCanvas (the
  // incognito guard lives at each call site). pr.meta.name == the passed name.
  void MainWindow::createProject(const QString& name) {
    Project pr;
    pr.meta.id = projectsStore_.createId(nowMs(), makeSalt());
    pr.meta.name = name.toStdString();
    pr.meta.createdAt = pr.meta.updatedAt = nowMs();
    pr.imagePath = canvas_->imagePath();
    pr.lines = canvas_->allLines();
    pr.cropRect = canvas_->cropRect();
    pr.rotationQuarters = canvas_->rotationQuarters();
    pr.meta.hasImage = !pr.imagePath.isEmpty();
    projectList_.push_back(pr);
    activeProjectId_ = QString::fromStdString(pr.meta.id);
    fileStore::saveProjects(projectList_);
    refreshActions();
    notify_->success(QString("Created \"%1\"").arg(name));
  }

  void MainWindow::saveToActiveProject() {
    if (incognito_) {  // S6: saving is blocked while incognito
      notify_->info("Incognito mode — saving is disabled");
      return;
    }
    if (activeProjectId_.isEmpty()) {
      newProjectFromCanvas();
      return;
    }
    const std::string id = activeProjectId_.toStdString();
    auto it = std::find_if(projectList_.begin(), projectList_.end(),
                           [&](const Project& p) { return p.meta.id == id; });
    if (it == projectList_.end()) {
      newProjectFromCanvas();
      return;
    }
    it->imagePath = canvas_->imagePath();
    it->lines = canvas_->allLines();
    it->cropRect = canvas_->cropRect();
    it->rotationQuarters = canvas_->rotationQuarters();
    it->meta.updatedAt = nowMs();
    it->meta.hasImage = !it->imagePath.isEmpty();
    fileStore::saveProjects(projectList_);
    notify_->success(
        QString("Saved to \"%1\"").arg(QString::fromStdString(it->meta.name)));
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

    // Re-apply to the live actions.
    for (auto it = hotkeyActions_.begin(); it != hotkeyActions_.end(); ++it) {
      const QString seq = hotkeys_.value(it.key(), hotkeyDefaults_.value(it.key()));
      it.value()->setShortcut(QKeySequence(seq));
    }
    notify_->success("Shortcuts updated");
  }

  void MainWindow::updateStatusIdle() {
    status_->setText(canvas_->hasImage()
                         ? "Ready"
                         : "Open an image — or create a blank one — to begin");
  }

}
