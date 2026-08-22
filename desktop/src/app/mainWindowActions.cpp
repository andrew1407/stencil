#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "dataExportController.hpp"
#include "incognitoOverlay.hpp"
#include "modalReveal.hpp"
#include "notifications.hpp"
#include "numericInput.hpp"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QActionGroup>
#include <QButtonGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>

// MainWindow's action set: buildActions() (the app-wide QActions + hotkeys) and
// buildContextActions() (the canvas context-menu set). Split from mainWindow.cpp;
// same class, definitions only.

namespace stencil::gui {

  // Copy the FOCUSED widget's text selection, if it has one — browser parity:
  // Ctrl/⌘C belongs to the selection under the cursor (a chat card, the
  // composer, any line edit) and only falls through to "copy the image" when
  // nothing text-like is selected. Returns true when it copied text.
  static bool copyFocusedSelection() {
    QWidget* f = QApplication::focusWidget();
    if (!f) return false;
    QString text;
    if (auto* label = qobject_cast<QLabel*>(f)) {
      if (label->hasSelectedText()) text = label->selectedText();
    } else if (auto* edit = qobject_cast<QLineEdit*>(f)) {
      if (edit->hasSelectedText()) text = edit->selectedText();
    } else if (auto* plain = qobject_cast<QPlainTextEdit*>(f)) {
      if (plain->textCursor().hasSelection()) text = plain->textCursor().selectedText();
    } else if (auto* rich = qobject_cast<QTextEdit*>(f)) {
      if (rich->textCursor().hasSelection()) text = rich->textCursor().selectedText();
    }
    if (text.isEmpty()) return false;
    // Qt reports paragraph breaks as U+2029 — restore real newlines.
    text.replace(QChar(0x2029), QLatin1Char('\n'));
    QApplication::clipboard()->setText(text);
    return true;
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
      bindRevealAnchor(a);
      return a;
    };

    // Richer tooltip than mk()'s default while KEEPING the hotkey suffix (browser
    // composeControlTitle). Matters on the icon-only toolbar, where the tooltip is
    // the only place the hotkey shows.
    auto tip = [this](QAction* a, const QString& desc) { setActionTip(a, desc); };

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
    // Short labels for the shared toolbar button (QToolButton renders iconText());
    // the menu entries keep the full "Start Drawing" / "Stop Drawing".
    actStartDraw_->setIconText("Start");
    actStopDraw_->setIconText("Stop");
    actNewLine_ = mk("New Line", "Alt+N");
    actUndo_ = mk("Undo", hotkey("undo", "Ctrl+Z"));
    actRedo_ = mk("Redo", hotkey("redo", "Ctrl+Shift+Z"));
    actDeleteLast_ = mk("Delete Last Point", "Backspace");
    // Selection deletes (shared hotkeysConfig deleteLine=Alt+Delete,
    // deletePoint=Alt+Shift+Delete). On macOS Delete→Backspace so ⌥⌫ / ⌥⇧⌫ work.
    actDeleteLine_ = mk("Delete Selected Line (Point if focused)",
                        platformizeSeq(hotkey("deleteLine", "Alt+Delete")));
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
    actFullscreen_ = mk("Enter Fullscreen", hotkey("fullscreen", "Alt+F"));
    actSettings_ = mk("Settings…", hotkey("openVisuals", "Alt+V"));
    actProjects_ = mk("Projects…", hotkey("openProjects", "Ctrl+Shift+P"));
    actConnect_ = mk("Servers…", hotkey("openServers", "Ctrl+Shift+K"));
    tip(actConnect_,
        "Connect to collaboration servers — shared projects appear with a golden outline");
    actLinks_ = mk("Image Links…", hotkey("openLinks", "Ctrl+Shift+L"));
    actOpenIn_ = mk("Open In…", hotkey("openIn", "Ctrl+Shift+E"));
    tip(actOpenIn_,
        "Open the current project in the browser app or the Telegram bot");
    // AI Assistant chat dock toggle (llm-contract.md). No shared
    // hotkeysConfig.json entry yet, so the default is set literally like New Line.
    actChat_ = mk("AI Assistant", hotkey("toggleChat", "Ctrl+Shift+A"));
    // Named to disambiguate from the dock's internal toggleViewAction (same
    // visible text) for the GUI e2e lookup.
    actChat_->setObjectName("actChat");
    actChat_->setCheckable(true);
    tip(actChat_, "Chat with the AI assistant — plan edits, variants, and layouts");
    // Animated reveal/dismiss (browser parity). A popover gesture (armed
    // popoverAnchor_) opens the COMPACT floating chat instead — that gesture
    // always means "show it compact here", so an unchecking trigger is re-checked.
    connect(actChat_, &QAction::toggled, this, [this](bool on) {
      QWidget* anchor = popoverAnchor_.data();
      popoverAnchor_.clear();
      if (anchor && chatDock_) {
        if (!on) {
          QSignalBlocker b(actChat_);
          actChat_->setChecked(true);
        }
        openChatCompact(anchor);
        return;
      }
      setChatShown(on, /*animate=*/true);
    });
    // New Project has no shared hotkeysConfig.json entry; literal default here.
    actNewProject_ = mk("New Project", "Ctrl+Shift+N");
    actSaveProject_ = mk("Save to Project", "Ctrl+Shift+S");
    // Trash: clear (remove) the current project/editor (mirrors the browser's
    // #clear-storage danger button). Hidden for server projects (refreshActions).
    actClearProject_ = mk("Clear Project", hotkey("clearProject", "Ctrl+Alt+R"));
    tip(actClearProject_, "Remove");
    // Rename the project from the keyboard — the same inline edit the ✎ beside the
    // toolbar name opens (enterNameEdit declines when there is nothing to rename).
    actRenameProject_ = mk("Rename Project", hotkey("renameProject", "Ctrl+Alt+N"));
    connect(actRenameProject_, &QAction::triggered, this, &MainWindow::enterNameEdit);
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
    // Both must carry the combos the shared registry defines for them, or the
    // Shortcuts window lists chords that do nothing.
    actStencilLiveSync_ = mk("Live Sync with File", hotkey("toggleLiveSync", "Ctrl+Shift+Y"));
    actStencilLiveSync_->setCheckable(true);
    actStencilLiveSync_->setEnabled(false);   // enabled once the project is linked to a .stencil file
    tip(actStencilLiveSync_, "Live sync this project to its .stencil file (auto-save + watch for changes)");
    actDeleteProjectFile_ = mk("Delete Project File (.stencil)", hotkey("deleteProject", "Ctrl+Shift+Backspace"));
    actDeleteProjectFile_->setEnabled(false);   // enabled once the project is linked to a .stencil file
    tip(actDeleteProjectFile_, "Delete the linked .stencil file from disk (the project stays open here)");
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
    connect(actDeleteProjectFile_, &QAction::triggered, this, [this] { deleteProjectFile(); });
    connect(actOpenProjectFile_, &QAction::triggered, this, [this] {
      const QString path = QFileDialog::getOpenFileName(
          this, "Open project", QString(), "Stencil project (*.stencil)");
      if (!path.isEmpty()) openProjectFile(path);
    });
    connect(actCopyLayout_, &QAction::triggered, this, [this] { dataExport_->copyLayout(); });
    connect(actPasteLayout_, &QAction::triggered, this, [this] { dataExport_->pasteLayout(); });
    connect(actSaveImage_, &QAction::triggered, this, [this] { dataExport_->saveImageFile(); });
    // Ctrl/⌘C belongs to whatever is SELECTED under the focus first (a chat
    // card's text, the composer, any field) — browser parity; copying the image
    // is the fallback when nothing text-like is selected.
    connect(actCopyImage_, &QAction::triggered, this, [this] {
      if (copyFocusedSelection()) return;
      dataExport_->copyImageToClipboard();
    });
    connect(actPasteImage_, &QAction::triggered, this, &MainWindow::pasteImage);

    // Incognito (S6): edit without saving. Togglable only before an image is
    // loaded (browser behavior), so it gets disabled once content exists.
    actIncognito_->setCheckable(true);
    // Through tip(), not setToolTip(): a plain set would drop the ⌥I that mk() had put on
    // it, and the icon-only toolbar is the only place that shortcut is written down.
    tip(actIncognito_,
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
    // Bare Backspace routes by selection: delete the SELECTED line(s) when not
    // mid-stroke, else "delete last point". On a MacBook "delete" IS Backspace,
    // so selected-line deletion must ride this key (browser controlsBinder twin).
    connect(actDeleteLast_, &QAction::triggered, this, [this] {
      if (!canvas_->isDrawing() && !canvas_->selectedIndices().empty())
        canvas_->deleteSelectedLine();
      else
        canvas_->deleteLastPoint();
    });
    // Alt+Delete routes by selection: a focused POINT narrows it to that point (the
    // line survives) — same shared-chord pattern as the bare Backspace above.
    connect(actDeleteLine_, &QAction::triggered, this, [this] {
      if (canvas_->selectedPoint() >= 0)
        canvas_->deletePoint(canvas_->selectedPoint());
      else
        canvas_->deleteSelectedLine();
    });
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
    });
    actShortcuts_ = mk("Customize Shortcuts…", hotkey("openHotkeys", "Alt+K"));
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
    hotkeyActions_["openVisuals"] = actSettings_;
    hotkeyActions_["openHotkeys"] = actShortcuts_;
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
    hotkeyActions_["clearProject"] = actClearProject_;
    hotkeyActions_["renameProject"] = actRenameProject_;
    hotkeyActions_["toggleTheme"] = actTheme_;
    hotkeyActions_["openHelp"] = actInfo_;
    hotkeyActions_["toggleLiveSync"] = actStencilLiveSync_;
    hotkeyActions_["deleteProject"] = actDeleteProjectFile_;

    // ── Tooltips: the browser's words, verbatim (toolbar.js data-title) —
    // menu LABELS stay untouched; desktop-only affordances keep their own wording.
    tip(actOpen_, "Open an image — local file, URL, or new blank");
    tip(actSaveImage_, "Download image");
    tip(actCopyImage_, "Copy image to clipboard");
    tip(actOpenIn_, "Open in another app");
    tip(actProjects_, "Projects");
    // The browser's twin adds "(Shift+click: without theme)"; this app has no such
    // modifier on Save, so the note stays out rather than promising it.
    tip(actSaveProjectFile_, "Save Project (.stencil) — image + layout + settings in one file");
    tip(actOpenProjectFile_, "Open Project (.stencil)");
    tip(actConnect_, "Servers — connect to share & co-edit projects");
    tip(actLinks_, "Source & resource links for the current image");
    tip(actChat_, "AI assistant — chat to edit the image");
    tip(actCrop_, "Crop image");
    tip(actRotateLeft_, "Rotate image left");
    tip(actRotateRight_, "Rotate image right");
    tip(actFit_, "Fit to window");
    tip(actDownloadJson_, "Download Layout JSON");
    tip(actUploadJson_, "Upload Layout JSON");
    tip(actCopyLayout_, "Copy full Layout JSON (lines + all applied edits)");
    tip(actTheme_, "Toggle dark / light theme");
    tip(actInfo_, "Controls & shortcuts help");

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

    // ── Style submenu (contextMenu.js:39-57): spinboxes in QWidgetActions + an
    // exclusive line-style radio group; all push canvas DEFAULTS only.
    // Row scaffold: returns the layout to fill (host QWidget = parentWidget()); sets `act`.
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
      spin = new ExprSpinBox(w);
      spin->setRange(lo, hi);
      lay->addStretch(1);
      lay->addWidget(spin);
    };
    styleRow("Point Size", pointSpin_, 1, 30, pointSizeAction_);
    styleRow("Line Thickness", thickSpin_, 1, 20, thicknessAction_);
    // Point / thickness commit on change (contextMenu.js:467-491): defaults +
    // persist + canvas redraw via setDefaults.
    connect(pointSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v) {
              settings_.defaultPointSize = v;
              if (pointSize_) {
                QSignalBlocker b(pointSize_);
                pointSize_->setValue(v);  // keep toolbar control in sync
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
      // Anchor on the toolbar tint swatch (visible whenever the custom filter is on);
      // revealDialog falls back per its contract when it is hidden.
      const QColor c =
          support::pickColorAnimated(filterColorValue_, this, "Tint color", filterColorBtn_);
      if (c.isValid()) applyTintColor(c);
    });

    // ── Tooltip toggles (contextMenu.js:96-107, 546-557). Hosted as real QCheckBoxes
    // in QWidgetActions (like the point/thickness spinbox rows) so a click flips them
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

}  // namespace stencil::gui
