// The window itself: construction, the hotkey table it loads, the context actions it keeps in sync
// and the clipboard paste. The rest of the shell lives in MainWindow{WireSignals,Popover,Open*}.cpp.
#include "mainWindowShellParts.hpp"

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
}  // namespace stencil::gui
