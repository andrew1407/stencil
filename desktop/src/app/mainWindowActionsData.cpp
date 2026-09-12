// MainWindow's data actions (clipboard + layout/image export), routed through DataExportController.
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

  namespace {
    // Ctrl/⌘C belongs to the text selection under the focus first (browser parity); true when it copied text.
    bool copyFocusedSelection() {
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
      // Qt reports paragraph breaks as U+2029.
      text.replace(QChar(0x2029), QLatin1Char('\n'));
      QApplication::clipboard()->setText(text);
      return true;
    }
  }  // namespace

  void MainWindow::createDataActions() {
    // Clipboard hotkeys come from hotkeysConfig.json so a rebind re-applies live; the file export/import are menu-only.
    actDownloadJson_ = newAction("Export Layout JSON…", hotkey("downloadJson", "Ctrl+Shift+J"));
    actUploadJson_ = newAction("Import Layout JSON…", hotkey("uploadJson", "Ctrl+Shift+U"));
    actSaveProjectFile_ = newAction("Save Project As… (.stencil)", hotkey("saveProject", "Ctrl+Shift+S"));
    actOpenProjectFile_ = newAction("Open Project… (.stencil)", hotkey("openProject", "Ctrl+Shift+F"));
    // Both must carry the registry's combos, or the Shortcuts window lists chords that do nothing.
    actStencilLiveSync_ = newAction("Live Sync with File", hotkey("toggleLiveSync", "Ctrl+Shift+Y"));
    actStencilLiveSync_->setCheckable(true);
    actStencilLiveSync_->setEnabled(false);   // enabled once the project is linked to a .stencil file
    setActionTip(actStencilLiveSync_, "Live sync this project to its .stencil file (auto-save + watch for changes)");
    actDeleteProjectFile_ = newAction("Delete Project File (.stencil)", hotkey("deleteProject", "Ctrl+Shift+Backspace"));
    actDeleteProjectFile_->setEnabled(false);   // enabled once the project is linked to a .stencil file
    setActionTip(actDeleteProjectFile_, "Delete the linked .stencil file from disk (the project stays open here)");
    actCopyLayout_ = newAction("Copy Layout JSON", hotkey("copyLayout", "Ctrl+Shift+Alt+C"));
    actPasteLayout_ = newAction("Paste Layout JSON", QString());
    // actSaveImage_ owns Ctrl+Shift+D: always "Current". actSaveImageSplit_ is hidden until a split compare view is active;
    // syncSplitCopyDownloadSlot() moves the shortcut onto it then, so it gets no initial shortcut here.
    actSaveImage_ = newAction("Current (Tint + Lines/Points)", hotkey("saveImage", "Ctrl+Shift+D"));
    actSaveImageSplit_ = newAction("With Compare", QString());
    actSaveImageSplit_->setVisible(false);
    actSaveImageOriginal_ = newAction("Original (No Tint, No Lines/Points)", hotkey("saveImageOriginal", "Ctrl+Alt+D"));
    // "Filter Only" renders identical to "Original" with no filter — hidden until one is active.
    actSaveImageTint_ = newAction("Filter Only (No Lines/Points)", hotkey("saveImageTint", "Ctrl+Shift+Alt+D"));
    actSaveImageTint_->setVisible(settings_.imageFilter != QLatin1String("none"));
    // actCopyImage_ owns Ctrl+C — mirrors actSaveImage_ above.
    actCopyImage_ = newAction("Current (Tint + Lines/Points)", hotkey("copyImage", "Ctrl+C"));
    actCopyImageSplit_ = newAction("With Compare", QString());
    actCopyImageSplit_->setVisible(false);
    actCopyImageOriginal_ = newAction("Original (No Tint, No Lines/Points)", hotkey("copyImageOriginal", "Ctrl+Shift+C"));
    actCopyImageTint_ = newAction("Filter Only (No Lines/Points)", hotkey("copyImageTint", "Ctrl+Alt+C"));
    actCopyImageTint_->setVisible(settings_.imageFilter != QLatin1String("none"));
    // "Current"'s OWN row, separate so it can hide without hiding the toolbar button; its TEXT mirrors the live combo. Hidden until there are lines/points.
    actSaveImageCurrentRow_ = newAction("Current (Tint + Lines/Points)", QString());
    actSaveImageCurrentRow_->setVisible(false);
    actCopyImageCurrentRow_ = newAction("Current (Tint + Lines/Points)", QString());
    actCopyImageCurrentRow_->setVisible(false);
    actShareImage_ = newAction("Share Image…", hotkey("shareImage", "Ctrl+Alt+S"));
    // Hidden where the OS has no share sheet (Linux); browser: utils.js supportsShareFiles().
    actShareImage_->setVisible(support::shareSheetAvailable());
    // Ctrl+V: image over layout JSON text (drawingApp.js :563-591); pasteImage() dispatches.
    actPasteImage_ = newAction("Paste (Image or Layout)", hotkey("paste", "Ctrl+V"));

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
    // The button itself, not the window — dataExportController.hpp shareImage().
    connect(actShareImage_, &QAction::triggered, this,
            [this] { dataExport_->shareImage(buttonForAction(actShareImage_)); });
    // Text selection under the focus first (browser parity); the image is the fallback.
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
