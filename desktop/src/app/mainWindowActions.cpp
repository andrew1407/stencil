#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "dataExportController.hpp"
#include "incognitoOverlay.hpp"
#include "modalReveal.hpp"
#include "notifications.hpp"
#include "numericInput.hpp"
#include "exportPreview.hpp"
#include "menuReveal.hpp"
#include "menuRowPolish.hpp"
#include "../support/modalChrome.hpp"   // confirmModal — the browser-styled question
#include "../support/shareImage.hpp"    // shareSheetAvailable — no Share button on Linux

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
#include <QMenu>
#include <QMouseEvent>
#include <QGuiApplication>
#include <functional>

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
      // Shown natively (⌘C on macOS, Ctrl+C elsewhere) and kept current across rebinds;
      // storage and matching keep the portable form via QKeySequence above.
      setActionTip(a, text);
      addAction(a);  // register the shortcut on the window
      bindRevealAnchor(a);
      return a;
    };

    // Richer tooltip than mk()'s default while KEEPING the hotkey suffix (browser
    // composeControlTitle). Matters on the icon-only toolbar, where the tooltip is
    // the only place the hotkey shows.
    auto tip = [this](QAction* a, const QString& desc) { setActionTip(a, desc); };

    // The unified Open dialog (local file, URL, or a new blank canvas), replacing the
    // former three-way split of Open Image / Open Another Image / New Blank Image —
    // mirroring the browser's one dialog behind both #load-image-btn and
    // #open-image-btn. actOpenAnother_ below opens the very same dialog, just from
    // the icon-only row button shown once an image is loaded.
    actOpen_ = mk("Open Image…", hotkey("loadImage", "Ctrl+O"));
    tip(actOpen_,
        "Open an image — a local file, a web URL, or a new blank canvas");
    // Same dialog as actOpen_, its own action so the toolbar can give it a distinct
    // icon/shortcut (browser parity: #open-image-btn vs #load-image-btn) — it's the
    // compact icon shown alongside Save/Copy/Share/Open-in once an image is loaded,
    // where actOpen_'s labelled button steps aside for the empty-state one.
    actOpenAnother_ = mk("Open Another Image…", hotkey("openAnotherImage", "Ctrl+Shift+O"));
    tip(actOpenAnother_,
        "Open another image — local file, URL, or new blank");
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
    // Start/Stop drawing: mirrors hotkeysConfig startDraw=Alt+A,
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
    actSettings_ = mk("Visuals && Settings…", hotkey("openVisuals", "Alt+V"));
    tip(actSettings_, "Default visuals & highlight styles");   // the browser #visuals-btn title
    actProjects_ = mk("Projects…", hotkey("openProjects", "Ctrl+Shift+P"));
    actConnect_ = mk("Servers…", hotkey("openServers", "Ctrl+Shift+K"));
    tip(actConnect_,
        "Connect to collaboration servers — shared projects appear with a golden outline");
    actLinks_ = mk("Image Links…", hotkey("openLinks", "Ctrl+Shift+L"));
    actDescription_ = mk("Project Description…", hotkey("openDescription", "Alt+Shift+D"));
    actKeywords_ = mk("Project Keywords…", hotkey("openKeywords", "Alt+Shift+K"));
    actOpenIn_ = mk("Open In…", hotkey("openIn", "Ctrl+Shift+E"));
    tip(actOpenIn_,
        "Open the current project in the browser app or the Telegram bot");
    // AI Assistant chat dock toggle (llm-contract.md; shared hotkeysConfig toggleChat).
    actChat_ = mk("AI Assistant", hotkey("toggleChat", "Alt+G"));
    // Named to disambiguate from the dock's internal toggleViewAction (same
    // visible text) for the GUI e2e lookup.
    actChat_->setObjectName("actChat");
    actChat_->setCheckable(true);
    tip(actChat_, "Chat with the AI assistant — plan edits, variants, and layouts");
    // Animated reveal/dismiss (browser parity). A popover gesture (armed
    // pop_.anchor) opens the COMPACT floating chat instead — that gesture
    // always means "show it compact here", so an unchecking trigger is re-checked.
    connect(actChat_, &QAction::toggled, this, [this](bool on) {
      QWidget* anchor = pop_.anchor.data();
      pop_.anchor.clear();
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
    tip(actClearProject_, "Remove current project");
    // Rename the project from the keyboard — the same inline edit the ✎ beside the
    // toolbar name opens (enterNameEdit declines when there is nothing to rename).
    actRenameProject_ = mk("Rename Project", hotkey("renameProject", "Ctrl+Alt+N"));
    connect(actRenameProject_, &QAction::triggered, this, &MainWindow::enterNameEdit);
    actSaveSession_ = mk("Save Session", "Ctrl+S");
    actInfo_ = mk("Controls && Shortcuts Info", hotkey("openHelp", "F1"));
    actIncognito_ = mk("Incognito", hotkey("toggleIncognito", "Alt+I"));
    actTooltip_ = mk("Show Tooltips", QString());   // browser label parity (was "Hover Tooltip")
    actTooltip_->setCheckable(true);
    // Allow-formulas toggle, also reachable from the View menu so the
    // f(x,y) inputs aren't lost when the toolbar overflows. Two-way synced with
    // the toolbar allowFormulas_ checkbox below.
    actAllowFormulas_ = mk("Allow Formulas", QString());
    actAllowFormulas_->setCheckable(true);
    actQuit_ = mk("Quit", "Ctrl+Q");

    // Data actions. The clipboard hotkeys come from hotkeysConfig.json
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
    // Ctrl+Alt+C moved to the new "tint only" image copy below — Copy Layout JSON
    // now sits on Ctrl+Shift+Alt+C (hotkeysConfig.json, shared with the browser).
    actCopyLayout_ = mk("Copy Layout JSON", hotkey("copyLayout", "Ctrl+Shift+Alt+C"));
    actPasteLayout_ = mk("Paste Layout JSON", QString());
    // actSaveImage_ owns Ctrl+Shift+D and the toolbar Download button — the app's
    // PRIMARY download gesture, always "Current" (tint + lines/points), comparing or not
    // (same as the browser and same as Ctrl+C below, always has). actSaveImageSplit_ is a
    // separate "With Compare" action, hidden until a split compare view is active —
    // syncSplitCopyDownloadSlot() shows it and moves the real Ctrl+Shift+D shortcut onto
    // it then, back onto actSaveImage_ when compare turns off. No initial shortcut here:
    // it only ever borrows actSaveImage_'s.
    actSaveImage_ = mk("Current (Tint + Lines/Points)", hotkey("saveImage", "Ctrl+Shift+D"));
    actSaveImageSplit_ = mk("With Compare", QString());
    actSaveImageSplit_->setVisible(false);
    actSaveImageOriginal_ = mk("Original (No Tint, No Lines/Points)", hotkey("saveImageOriginal", "Ctrl+Alt+D"));
    // "Filter Only" would render byte-identical to "Original" with no filter applied —
    // hidden until one actually is (Settings{}'s default imageFilter is "none"; kept live
    // by applyImageFilter()/refreshActions()/syncContextActions().
    actSaveImageTint_ = mk("Filter Only (No Lines/Points)", hotkey("saveImageTint", "Ctrl+Shift+Alt+D"));
    actSaveImageTint_->setVisible(settings_.imageFilter != QLatin1String("none"));
    // actCopyImage_ owns Ctrl+C and the toolbar Copy button — mirrors actSaveImage_ above:
    // always "Current", with actCopyImageSplit_ as its own "With Compare" sibling.
    actCopyImage_ = mk("Current (Tint + Lines/Points)", hotkey("copyImage", "Ctrl+C"));
    actCopyImageSplit_ = mk("With Compare", QString());
    actCopyImageSplit_->setVisible(false);
    actCopyImageOriginal_ = mk("Original (No Tint, No Lines/Points)", hotkey("copyImageOriginal", "Ctrl+Shift+C"));
    actCopyImageTint_ = mk("Filter Only (No Lines/Points)", hotkey("copyImageTint", "Ctrl+Alt+C"));
    actCopyImageTint_->setVisible(settings_.imageFilter != QLatin1String("none"));
    // "Current"'s OWN row (see mainWindow.hpp) — a separate action from actSaveImage_/
    // actCopyImage_ above so it can be hidden without hiding the toolbar button. No
    // shortcut of its own; syncSplitCopyDownloadSlot() keeps its TEXT mirroring
    // whichever combo is live on the real action. Hidden until there ARE lines/points
    // (Settings{}'s canvas starts empty).
    actSaveImageCurrentRow_ = mk("Current (Tint + Lines/Points)", QString());
    actSaveImageCurrentRow_->setVisible(false);
    actCopyImageCurrentRow_ = mk("Current (Tint + Lines/Points)", QString());
    actCopyImageCurrentRow_->setVisible(false);
    actShareImage_ = mk("Share Image…", hotkey("shareImage", "Ctrl+Alt+S"));
    // Hidden where the OS has no share sheet (Linux) — icon, menu row and, since Qt
    // never enables an invisible action, the chord too. Browser parity: utils.js
    // supportsShareFiles() gates #share-image exactly so.
    actShareImage_->setVisible(support::shareSheetAvailable());
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
    connect(actSaveImage_, &QAction::triggered, this, [this] { dataExport_->saveImageFile("current"); });
    connect(actSaveImageCurrentRow_, &QAction::triggered, this, [this] { dataExport_->saveImageFile("current"); });
    connect(actSaveImageSplit_, &QAction::triggered, this, [this] { dataExport_->saveImageFile("split"); });
    connect(actSaveImageOriginal_, &QAction::triggered, this, [this] { dataExport_->saveImageFile("original"); });
    connect(actSaveImageTint_, &QAction::triggered, this, [this] { dataExport_->saveImageFile("tint"); });
    // The button itself, not the window — see dataExportController.hpp shareImage().
    connect(actShareImage_, &QAction::triggered, this,
            [this] { dataExport_->shareImage(buttonForAction(actShareImage_)); });
    // Ctrl/⌘C (and its Shift/Alt siblings) belong to whatever is SELECTED under the
    // focus first (a chat card's text, the composer, any field) — browser parity;
    // copying the image is the fallback when nothing text-like is selected.
    connect(actCopyImage_, &QAction::triggered, this, [this] {
      if (copyFocusedSelection()) return;
      dataExport_->copyImageToClipboard("current");
    });
    connect(actCopyImageCurrentRow_, &QAction::triggered, this, [this] {
      if (copyFocusedSelection()) return;
      dataExport_->copyImageToClipboard("current");
    });
    connect(actCopyImageSplit_, &QAction::triggered, this, [this] {
      if (copyFocusedSelection()) return;
      dataExport_->copyImageToClipboard("split");
    });
    connect(actCopyImageOriginal_, &QAction::triggered, this, [this] {
      if (copyFocusedSelection()) return;
      dataExport_->copyImageToClipboard("original");
    });
    connect(actCopyImageTint_, &QAction::triggered, this, [this] {
      if (copyFocusedSelection()) return;
      dataExport_->copyImageToClipboard("tint");
    });
    connect(actPasteImage_, &QAction::triggered, this, &MainWindow::pasteImage);

    // Incognito: edit without saving. Togglable only before an image is
    // loaded (browser behavior), so it gets disabled once content exists.
    actIncognito_->setCheckable(true);
    // Through tip(), not setToolTip(): a plain set would drop the ⌥I that mk() had put on
    // it, and the icon-only toolbar is the only place that shortcut is written down.
    tip(actIncognito_, "Incognito — edit without saving");
    // …and WHY it is greyed out, as the amber reason line (browser: data-disabled-reason) —
    // inside the title it read as part of what the button does.
    setTipReason(actIncognito_, "Choose incognito before adding an image");

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
    connect(actOpenAnother_, &QAction::triggered, this, &MainWindow::openImage);
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
    // Browser parity (drawingApp.js clearAllLines): a wipe of every line asks first, in
    // the same styled confirm the Clear-project trash uses, and says so when declined.
    connect(actClearAll_, &QAction::triggered, this, [this] {
      ConfirmSpec spec;
      spec.title = tr("Clear all lines");
      spec.message = tr("Wipe ALL lines from the canvas? This cannot be undone except via Undo.");
      spec.confirmIcon = QStringLiteral("trash");
      spec.danger = true;
      if (!confirmModal(this, spec)) {
        if (notify_) notify_->info(tr("Clear canceled"));   // declined = a notice, not a failure
        return;
      }
      canvas_->clearAll();
    });
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
    fs_.hoverTimer = new QTimer(this);   // drives the fullscreen edge-hover reveal
    connect(fs_.hoverTimer, &QTimer::timeout, this, &MainWindow::fsHoverTick);
    connect(actSettings_, &QAction::triggered, this, &MainWindow::openSettings);
    connect(actProjects_, &QAction::triggered, this, &MainWindow::openProjects);
    connect(actConnect_, &QAction::triggered, this, &MainWindow::openConnections);
    connect(actLinks_, &QAction::triggered, this, &MainWindow::openLinks);
    connect(actDescription_, &QAction::triggered, this, &MainWindow::openDescription);
    connect(actKeywords_, &QAction::triggered, this, &MainWindow::openKeywords);
    // The assistant's own settings dialog (the chat's … menu ▸ Settings) on a chord of its
    // own — shared hotkeysConfig openAssistantSettings, so the browser answers the same keys.
    actAssistantSettings_ = mk("AI Assistant Settings…", hotkey("openAssistantSettings", "Alt+Shift+G"));
    tip(actAssistantSettings_, "AI assistant settings — provider, model & voice");
    connect(actAssistantSettings_, &QAction::triggered, this, &MainWindow::openAssistantSettings);
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
    actShortcuts_ = mk("Keyboard Shortcuts…", hotkey("openHotkeys", "Alt+K"));
    tip(actShortcuts_, "Keyboard shortcuts");   // the browser #settings-btn title
    connect(actShortcuts_, &QAction::triggered, this,
            &MainWindow::openShortcuts);
    // Keyboard route to the canvas right-click menu (shared hotkeysConfig contextMenu).
    // Registered on the window only — no menu-bar entry, the menu IS the entry.
    actContextMenu_ = mk("Canvas Context Menu", hotkey("contextMenu", "Shift+F10"));
    tip(actContextMenu_, "Open the canvas context menu at the pointer (or the canvas centre)");
    connect(actContextMenu_, &QAction::triggered, this, &MainWindow::showContextMenuFromKeyboard);

    // Map hotkey ids -> their actions so a rebind can re-apply live. Only
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
    hotkeyActions_["openAssistantSettings"] = actAssistantSettings_;
    hotkeyActions_["contextMenu"] = actContextMenu_;
    hotkeyActions_["resetZoom"] = actFit_;
    hotkeyActions_["zoomIn"] = actZoomIn_;
    hotkeyActions_["zoomOut"] = actZoomOut_;
    hotkeyActions_["undo"] = actUndo_;
    hotkeyActions_["redo"] = actRedo_;
    // Data clipboard hotkeys (hotkeysConfig.json copyImage/copyLayout/paste).
    hotkeyActions_["copyImage"] = actCopyImage_;
    hotkeyActions_["copyImageOriginal"] = actCopyImageOriginal_;
    hotkeyActions_["copyImageTint"] = actCopyImageTint_;
    hotkeyActions_["copyLayout"] = actCopyLayout_;
    hotkeyActions_["paste"] = actPasteImage_;
    // File / project hotkeys whose defaults live in the shared hotkeysConfig.json
    // (coordinated with the browser), wired so a rebind re-applies live and they
    // appear in the Keyboard Shortcuts dialog.
    hotkeyActions_["cropImage"] = actCrop_;
    hotkeyActions_["saveImage"] = actSaveImage_;
    hotkeyActions_["saveImageOriginal"] = actSaveImageOriginal_;
    hotkeyActions_["saveImageTint"] = actSaveImageTint_;
    hotkeyActions_["shareImage"] = actShareImage_;
    hotkeyActions_["downloadJson"] = actDownloadJson_;
    hotkeyActions_["uploadJson"] = actUploadJson_;
    hotkeyActions_["saveProject"] = actSaveProjectFile_;
    hotkeyActions_["openProject"] = actOpenProjectFile_;
    hotkeyActions_["openServers"] = actConnect_;
    hotkeyActions_["openLinks"] = actLinks_;
    hotkeyActions_["openDescription"] = actDescription_;
    hotkeyActions_["openKeywords"] = actKeywords_;
    hotkeyActions_["toggleIncognito"] = actIncognito_;
    hotkeyActions_["loadImage"] = actOpen_;
    hotkeyActions_["openAnotherImage"] = actOpenAnother_;
    hotkeyActions_["openProjects"] = actProjects_;
    hotkeyActions_["clearProject"] = actClearProject_;
    hotkeyActions_["renameProject"] = actRenameProject_;
    hotkeyActions_["toggleTheme"] = actTheme_;
    hotkeyActions_["openHelp"] = actInfo_;
    hotkeyActions_["toggleLiveSync"] = actStencilLiveSync_;
    hotkeyActions_["deleteProject"] = actDeleteProjectFile_;

    // Tooltips: the browser's words, verbatim (toolbar.js data-title) —
    // menu LABELS stay untouched; desktop-only affordances keep their own wording.
    tip(actOpen_, "Open an image — local file, URL, or new blank");
    tip(actOpenAnother_, "Open another image — local file, URL, or new blank");
    tip(actSaveImage_, "Download image · Right-click for download options");
    tip(actCopyImage_, "Copy image to clipboard · Right-click for copy options");
    tip(actSaveImageSplit_, "Download the split compare view");
    tip(actCopyImageSplit_, "Copy the split compare view");
    tip(actSaveImageCurrentRow_, "Download image · Right-click for download options");
    tip(actCopyImageCurrentRow_, "Copy image to clipboard · Right-click for copy options");
    tip(actSaveImageOriginal_, "Download the original image — no tint, no lines/points");
    tip(actSaveImageTint_, "Download the filtered image — no lines/points");
    tip(actCopyImageOriginal_, "Copy the original image — no tint, no lines/points");
    tip(actCopyImageTint_, "Copy the filtered image — no lines/points");
    tip(actShareImage_, "Share image");
    tip(actOpenIn_, "Open in another app");
    tip(actProjects_, "Projects");
    // The browser's twin adds "(Shift+click: without theme)"; this app has no such
    // modifier on Save, so the note stays out rather than promising it.
    tip(actSaveProjectFile_, "Save Project (.stencil) — image + layout + settings in one file");
    tip(actOpenProjectFile_, "Open Project (.stencil)");
    tip(actConnect_, "Servers — connect to share & co-edit projects");
    tip(actLinks_, "Source & resource links for the current image");
    tip(actDescription_, "Project description");
    tip(actKeywords_, "Project keywords");
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
    // …and why each is greyed out, verbatim too (toolbar.js data-disabled-reason): the
    // "— reason" line joins the tooltip while the action is disabled and leaves with it.
    const auto why = [](QAction* a, const char* reason) { setTipReason(a, reason); };
    why(actSaveImage_, "Load an image to download it");
    why(actSaveImageCurrentRow_, "Load an image to download it");
    why(actCopyImage_, "Load an image to copy it");
    why(actCopyImageCurrentRow_, "Load an image to copy it");
    why(actSaveProjectFile_, "Open an image first");
    why(actDeleteProjectFile_, "Open or save a .stencil file first");
    why(actStencilLiveSync_, "Open or save a .stencil file first");
    // The DESCRIPTION & ATTRIBUTES trio edits a SAVED project's metadata (updateProjectTitle).
    why(actDescription_, "Save the project first to add a description");
    why(actKeywords_, "Save the project first to add keywords");
    why(actLinks_, "Save the project first to add links");
    why(actCrop_, "Load an image to crop");
    why(actRotateLeft_, "Load an image to rotate");
    why(actRotateRight_, "Load an image to rotate");
    why(actUndo_, "Nothing to undo");
    why(actRedo_, "Nothing to redo");
    why(actStartDraw_, "Load an image to start drawing");
    why(actClearAll_, "No lines to clear");
    why(actZoomIn_, "Load an image to zoom");
    why(actZoomOut_, "Load an image to zoom");
    why(actFit_, "Load an image to zoom");
    why(actDownloadJson_, "Draw at least one line to export");
    why(actCopyLayout_, "Draw at least one line to copy");
    why(actUploadJson_, "Load an image first");
    why(actClearProject_, "Open an image first — nothing to remove");

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

  // Persistent context-menu submenu actions. Port of the wiring done once
  // in browser/js/ui/contextMenu.js wire() (~112-605): the instant line/rect
  // items, and the Style / Image-Filter / Tooltip submenus.
  // Built once and reused on every right-click; showContextMenu() only re-syncs
  // their checked/enabled/visible state before exec (mirroring syncState ~239).
  void MainWindow::buildContextActions() {
    // Instant line/rect (contextMenu.js ctx-draw-line/ctx-draw-rect): set the
    // mode and begin drawing immediately. Two fixed actions rather than a toggle
    // that only picked the mode — each row always does exactly what it says, and
    // each carries its own animated outline glyph (browser parity).
    actDrawLineNow_ = new QAction("Draw Line", this);
    connect(actDrawLineNow_, &QAction::triggered, this, [this] {
      if (!canvas_->hasImage()) {
        notify_->error("Load an image first");
        return;
      }
      canvas_->setDrawMode(CanvasWidget::DrawMode::Line);
      canvas_->startDrawingMode();  // continues the selected line if one is set
      notify_->info("Drag to draw a line");
    });

    actDrawRectNow_ = new QAction("Draw Rectangle", this);
    connect(actDrawRectNow_, &QAction::triggered, this, [this] {
      if (!canvas_->hasImage()) {
        notify_->error("Load an image first");
        return;
      }
      canvas_->setDrawMode(CanvasWidget::DrawMode::Rect);
      canvas_->startDrawingMode();  // continues the selected line if one is set
      notify_->info("Drag to draw a rectangle");
    });

    // Style submenu (contextMenu.js:39-57): spinboxes in QWidgetActions + an
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
    // Context-menu sub-label (browser parity: contextMenu.js's .ctx-sub-label — "Image",
    // "Line Style", …): plain muted caption text, same row indent as every other row via
    // makeMenuRow, NOT QMenu::addSection() — a section draws its OWN separator line, which
    // the browser's caption never has (mainWindow.hpp's member comment explains why).
    auto makeSectionLabel = [this, &makeMenuRow](const QString& text) -> QWidgetAction* {
      QWidgetAction* act = nullptr;
      auto* lay = makeMenuRow(act, 6, 2);
      auto* label = new QLabel(text.toUpper(), lay->parentWidget());
      label->setObjectName(QStringLiteral("panelSectionHeader"));
      lay->addWidget(label);
      return act;
    };
    secImageAct_ = makeSectionLabel("Image");
    secLayoutJsonAct_ = makeSectionLabel("Layout (JSON)");
    secLineStyleAct_ = makeSectionLabel("Line Style");
    secFilterAct_ = makeSectionLabel("Filter");
    secCoordFormulasAct_ = makeSectionLabel("Coordinate Formulas");
    secShowInTooltipAct_ = makeSectionLabel("Show in Tooltip");

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

    // Image Filter submenu (contextMenu.js:59-74, 504-526). Exclusive radio
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
      // The tint row appears/disappears with the Custom Tint pick while the menu is up
      // (browser parity: contextMenu.js toggles .ctx-tint-visible on the radio change) —
      // syncContextActions() only sets it for the NEXT open. QMenu re-lays itself out on
      // an action's visibility change, so the flyout grows/shrinks in place.
      connect(rb, &QRadioButton::toggled, this, [this, value](bool on) {
        if (!on) return;
        applyImageFilter(value);
        if (tintColorAction_) tintColorAction_->setVisible(value == "custom");
      });
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

    // Tooltip toggles (contextMenu.js:96-107, 546-557). Hosted as real QCheckBoxes
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

    // Transformation submenu formula controls (contextMenu.js:84-100): an "Allow Formulas"
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

    // Units (View ▸ Units): cm | inches, exclusive, persisted in settings_.
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
    units_.unitCm = mkUnit("Centimeters (cm)", "cm");
    units_.unitIn = mkUnit("Inches (in)", "in");
  }

  // The rendered preview image for one export-variant QAction — maps the action
  // pointer to its variant string. Shared by the context menu's nested submenus and
  // the toolbar popups (wireExportPreviewHover uses it for every Alt-hover). Null
  // (and QImage::isNull()) for any action that isn't one of ours.
  QImage MainWindow::exportVariantPreviewImage(QAction* act) const {
    struct Spec { QAction* action; const char* variant; };
    const Spec specs[] = {
        {actCopyImage_, "current"},
        {actSaveImage_, "current"},
        {actCopyImageCurrentRow_, "current"},
        {actSaveImageCurrentRow_, "current"},
        {actCopyImageSplit_, "split"},
        {actSaveImageSplit_, "split"},
        {actCopyImageOriginal_, "original"},
        {actCopyImageTint_, "tint"},
        {actSaveImageOriginal_, "original"},
        {actSaveImageTint_, "tint"},
    };
    for (const auto& s : specs)
      if (s.action == act) return canvas_->renderToImage(QString::fromLatin1(s.variant));
    return QImage();
  }

  namespace {
    // Alt+hover preview for the same row (no new QMenu::hovered fires for a modifier
    // change). Watches `menu` AND every ancestor QMenu: a hover-opened submenu holds no
    // key grab of its own, so Qt can deliver a bare Alt to the chain's root instead.
    // Consumes only the Alt press/release itself.
    class AltPreviewFilter : public QObject {
     public:
      AltPreviewFilter(QMenu* menu, std::function<QImage(QAction*)> renderFor)
          : QObject(menu), menu_(menu), renderFor_(std::move(renderFor)) {
        for (QWidget* w = menu; qobject_cast<QMenu*>(w); w = w->parentWidget()) {
          w->installEventFilter(this);
          watched_.push_back(w);
        }
      }

     protected:
      bool eventFilter(QObject* obj, QEvent* e) override {
        if (!watched_.contains(qobject_cast<QWidget*>(obj))) return false;
        // Gliding OFF the previewed row hides the preview (browser wireAltPreview
        // mouseleave parity): the open menu owns all pointer traffic, so its own moves
        // are the hover-out signal; landing on another previewable row re-shows it.
        if (e->type() == QEvent::MouseMove && support::exportPreviewOwner() == menu_) {
          auto* mm = qobject_cast<QMenu*>(obj);
          QAction* act =
              mm ? mm->actionAt(static_cast<QMouseEvent*>(e)->position().toPoint()) : nullptr;
          if (mm != menu_ || !act || mm->actionGeometry(act) != support::exportPreviewOwnerRect())
            support::hideExportPreview();
        }
        if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Alt) {
          // Autorepeats still consumed (bare Alt must stay off the menu bar), but they
          // must not re-show — a platform that repeats a held modifier would replay
          // the preview's appearance for as long as Alt is down.
          if (QAction* act = static_cast<QKeyEvent*>(e)->isAutoRepeat() ? nullptr
                                                                        : menu_->activeAction()) {
            const QImage img = renderFor_(act);
            // A KEY-triggered appearance forms from the CURSOR (the row's centre is
            // the pointer-driven flights' origin) — user decision, both surfaces.
            if (!img.isNull())
              support::showExportPreview(img, menu_, menu_->actionGeometry(act), QCursor::pos());
          }
          // Consumed: a bare Alt reaching the menu bar enters mnemonic mode, stealing
          // focus and closing this popup (browser parity: popover.js preventDefault).
          return true;
        } else if (e->type() == QEvent::KeyRelease && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Alt) {
          // Released Alt pours the preview back into the CURSOR, mirroring the press.
          if (!static_cast<QKeyEvent*>(e)->isAutoRepeat())
            support::hideExportPreview(QCursor::pos());
          return true;
        }
        return false;
      }

     private:
      QMenu* menu_;
      QVector<QWidget*> watched_;
      std::function<QImage(QAction*)> renderFor_;
    };

    // Double-click / right-click on a copy/download-image TOOLBAR button opens its
    // export-options popup instead of the plain single-click action — the same split
    // browser's toolbar copy/download buttons use (js/ui/exportOptionsMenu.js). A
    // plain click is deferred (swallowed press/release + a short timer) so a following
    // dblclick can still cancel it; the button's own defaultAction sync (icon/tooltip/
    // enabled state) is untouched — only the trigger path is taken over.
    class ExportPopupFilter : public QObject {
     public:
      ExportPopupFilter(QToolButton* btn, QAction* act, QMenu* menu)
          : QObject(btn), btn_(btn), act_(act), menu_(menu) {
        timer_.setSingleShot(true);
        timer_.setInterval(250);
        QObject::connect(&timer_, &QTimer::timeout, this, [this] {
          if (act_->isEnabled()) act_->trigger();
        });
      }

     protected:
      bool eventFilter(QObject* obj, QEvent* e) override {
        if (obj != btn_) return false;
        switch (e->type()) {
          case QEvent::MouseButtonPress:
            return static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton;
          case QEvent::MouseButtonRelease:
            if (static_cast<QMouseEvent*>(e)->button() != Qt::LeftButton) return false;
            if (act_->isEnabled()) timer_.start();
            return true;
          case QEvent::MouseButtonDblClick:
            if (static_cast<QMouseEvent*>(e)->button() != Qt::LeftButton) return false;
            popup();
            return true;
          case QEvent::ContextMenu:
            popup();
            return true;
          default:
            return false;
        }
      }

     private:
      void popup() {
        timer_.stop();
        if (!act_->isEnabled()) return;
        menu_->popup(btn_->mapToGlobal(QPoint(0, btn_->height())));
      }
      QToolButton* btn_;
      QAction* act_;
      QMenu* menu_;
      QTimer timer_;
    };
  }  // namespace

  // One export-variant menu, identical on every surface: "With Compare" leads (visible
  // only while comparing — syncSplitCopyDownloadSlot toggles it), then "Current"'s OWN
  // row (hidden with nothing drawn; NOT actCopyImage_/actSaveImage_ themselves, which
  // stay the toolbar buttons' real actions), then the two fixed variants — Copy lists
  // Filter Only before Original, Download the reverse (browser exportOptionsMenu.js).
  void MainWindow::populateExportVariantMenu(QMenu* menu, bool copy) {
    if (copy) {
      menu->addAction(actCopyImageSplit_);
      menu->addAction(actCopyImageCurrentRow_);
      menu->addAction(actCopyImageTint_);
      menu->addAction(actCopyImageOriginal_);
    } else {
      menu->addAction(actSaveImageSplit_);
      menu->addAction(actSaveImageCurrentRow_);
      menu->addAction(actSaveImageOriginal_);
      menu->addAction(actSaveImageTint_);
    }
    wireExportPreviewHover(menu);
  }

  // Wire the Alt+hover live preview onto one export-variant menu (a nested context-menu
  // submenu, or a toolbar options popup) — call once, right after its actions are added.
  void MainWindow::wireExportPreviewHover(QMenu* menu) {
    // One render per row per menu-open: QMenu::hovered re-fires on every mouse move,
    // and renderToImage is a full native-resolution composite. Cleared on open AND
    // close, so a fresh open always renders against the current canvas.
    auto cache = std::make_shared<QHash<QAction*, QImage>>();
    auto renderFor = [this, cache](QAction* act) {
      const auto it = cache->constFind(act);
      if (it != cache->constEnd()) return it.value();
      const QImage img = exportVariantPreviewImage(act);
      cache->insert(act, img);
      return img;
    };
    connect(menu, &QMenu::hovered, this, [menu, renderFor](QAction* act) {
      if (QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier)) {
        const QImage img = renderFor(act);
        if (!img.isNull()) { support::showExportPreview(img, menu, menu->actionGeometry(act)); return; }
      }
      support::hideExportPreview();
    });
    connect(menu, &QMenu::aboutToShow, this, [cache] { cache->clear(); });
    connect(menu, &QMenu::aboutToHide, this, [cache] {
      cache->clear();
      support::hideExportPreview();
    });
    menu->installEventFilter(new AltPreviewFilter(menu, renderFor));
  }

  // The two toolbar buttons' own export-options popups (browser parity:
  // js/ui/exportOptionsMenu.js) — built once, reused on every double-click/right-click.
  // Called once, right after buildToolbar() (needs the live buttons).
  void MainWindow::wireExportOptionsPopups() {
    auto buildMenu = [this](bool copy) {
      auto* m = new QMenu(this);
      populateExportVariantMenu(m, copy);
      support::wireMenuRowPolish(m, this, /*compact=*/true);
      return m;
    };
    copyImageOptionsMenu_ = buildMenu(/*copy=*/true);
    saveImageOptionsMenu_ = buildMenu(/*copy=*/false);

    // NOT buttonForAction() — that only finds a CURRENTLY VISIBLE button, and with no
    // image loaded yet (construction time) the Image cluster's buttons start hidden
    // (mainWindowToolbar.cpp makeToolSection). The QToolButton object itself is built
    // once and persists (only its visibility toggles later), so any match by
    // defaultAction() — visible or not — is the right, permanent one to wire.
    auto wireButton = [this](QAction* act, QMenu* menu) {
      QToolButton* btn = nullptr;
      for (QToolButton* b : findChildren<QToolButton*>())
        if (b->defaultAction() == act) { btn = b; break; }
      if (!btn) return;
      support::revealMenuFrom(*menu, btn);
      btn->installEventFilter(new ExportPopupFilter(btn, act, menu));
    };
    wireButton(actCopyImage_, copyImageOptionsMenu_);
    wireButton(actSaveImage_, saveImageOptionsMenu_);
  }

}  // namespace stencil::gui
