// MainWindow's data actions: the clipboard + layout/image export set, which routes through
// DataExportController. Part of the buildActions() phase chain (mainWindowActions.cpp).
#include "mainWindow.hpp"
#include "canvasWidget.hpp"
#include "dataExportController.hpp"
#include "notifications.hpp"
#include "../support/shareImage.hpp"    // shareSheetAvailable — no Share button on Linux
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

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

  void MainWindow::createDataActions() {
    // Data actions. The clipboard hotkeys come from hotkeysConfig.json
    // (copyImage=Ctrl+C, copyLayout=Alt+J, paste=Ctrl+V) so a rebind re-applies
    // live; the JSON file export/import are menu-only (no browser hotkey).
    actDownloadJson_ = newAction("Export Layout JSON…", hotkey("downloadJson", "Ctrl+Shift+J"));
    actUploadJson_ = newAction("Import Layout JSON…", hotkey("uploadJson", "Ctrl+Shift+U"));
    // Whole-project files (.stencil): image + layout + settings + theme in one portable file.
    actSaveProjectFile_ = newAction("Save Project As… (.stencil)", hotkey("saveProject", "Ctrl+Shift+S"));
    actOpenProjectFile_ = newAction("Open Project… (.stencil)", hotkey("openProject", "Ctrl+Shift+F"));
    // Both must carry the combos the shared registry defines for them, or the
    // Shortcuts window lists chords that do nothing.
    actStencilLiveSync_ = newAction("Live Sync with File", hotkey("toggleLiveSync", "Ctrl+Shift+Y"));
    actStencilLiveSync_->setCheckable(true);
    actStencilLiveSync_->setEnabled(false);   // enabled once the project is linked to a .stencil file
    setActionTip(actStencilLiveSync_, "Live sync this project to its .stencil file (auto-save + watch for changes)");
    actDeleteProjectFile_ = newAction("Delete Project File (.stencil)", hotkey("deleteProject", "Ctrl+Shift+Backspace"));
    actDeleteProjectFile_->setEnabled(false);   // enabled once the project is linked to a .stencil file
    setActionTip(actDeleteProjectFile_, "Delete the linked .stencil file from disk (the project stays open here)");
    // Ctrl+Alt+C moved to the new "tint only" image copy below — Copy Layout JSON
    // now sits on Ctrl+Shift+Alt+C (hotkeysConfig.json, shared with the browser).
    actCopyLayout_ = newAction("Copy Layout JSON", hotkey("copyLayout", "Ctrl+Shift+Alt+C"));
    actPasteLayout_ = newAction("Paste Layout JSON", QString());
    // actSaveImage_ owns Ctrl+Shift+D and the toolbar Download button — the app's
    // PRIMARY download gesture, always "Current" (tint + lines/points), comparing or not
    // (same as the browser and same as Ctrl+C below, always has). actSaveImageSplit_ is a
    // separate "With Compare" action, hidden until a split compare view is active —
    // syncSplitCopyDownloadSlot() shows it and moves the real Ctrl+Shift+D shortcut onto
    // it then, back onto actSaveImage_ when compare turns off. No initial shortcut here:
    // it only ever borrows actSaveImage_'s.
    actSaveImage_ = newAction("Current (Tint + Lines/Points)", hotkey("saveImage", "Ctrl+Shift+D"));
    actSaveImageSplit_ = newAction("With Compare", QString());
    actSaveImageSplit_->setVisible(false);
    actSaveImageOriginal_ = newAction("Original (No Tint, No Lines/Points)", hotkey("saveImageOriginal", "Ctrl+Alt+D"));
    // "Filter Only" would render byte-identical to "Original" with no filter applied —
    // hidden until one actually is (Settings{}'s default imageFilter is "none"; kept live
    // by applyImageFilter()/refreshActions()/syncContextActions().
    actSaveImageTint_ = newAction("Filter Only (No Lines/Points)", hotkey("saveImageTint", "Ctrl+Shift+Alt+D"));
    actSaveImageTint_->setVisible(settings_.imageFilter != QLatin1String("none"));
    // actCopyImage_ owns Ctrl+C and the toolbar Copy button — mirrors actSaveImage_ above:
    // always "Current", with actCopyImageSplit_ as its own "With Compare" sibling.
    actCopyImage_ = newAction("Current (Tint + Lines/Points)", hotkey("copyImage", "Ctrl+C"));
    actCopyImageSplit_ = newAction("With Compare", QString());
    actCopyImageSplit_->setVisible(false);
    actCopyImageOriginal_ = newAction("Original (No Tint, No Lines/Points)", hotkey("copyImageOriginal", "Ctrl+Shift+C"));
    actCopyImageTint_ = newAction("Filter Only (No Lines/Points)", hotkey("copyImageTint", "Ctrl+Alt+C"));
    actCopyImageTint_->setVisible(settings_.imageFilter != QLatin1String("none"));
    // "Current"'s OWN row (see mainWindow.hpp) — a separate action from actSaveImage_/
    // actCopyImage_ above so it can be hidden without hiding the toolbar button. No
    // shortcut of its own; syncSplitCopyDownloadSlot() keeps its TEXT mirroring
    // whichever combo is live on the real action. Hidden until there ARE lines/points
    // (Settings{}'s canvas starts empty).
    actSaveImageCurrentRow_ = newAction("Current (Tint + Lines/Points)", QString());
    actSaveImageCurrentRow_->setVisible(false);
    actCopyImageCurrentRow_ = newAction("Current (Tint + Lines/Points)", QString());
    actCopyImageCurrentRow_->setVisible(false);
    actShareImage_ = newAction("Share Image…", hotkey("shareImage", "Ctrl+Alt+S"));
    // Hidden where the OS has no share sheet (Linux) — icon, menu row and, since Qt
    // never enables an invisible action, the chord too. Browser parity: utils.js
    // supportsShareFiles() gates #share-image exactly so.
    actShareImage_->setVisible(support::shareSheetAvailable());
    // Single Ctrl+V entrypoint (paste hotkey): image takes priority over a layout
    // JSON text payload, mirroring the browser paste listener (drawingApp.js
    // :563-591). pasteImage() does that dispatch.
    actPasteImage_ = newAction("Paste (Image or Layout)", hotkey("paste", "Ctrl+V"));

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

  }

}  // namespace stencil::gui
