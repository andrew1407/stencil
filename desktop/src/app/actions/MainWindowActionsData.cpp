// MainWindow's data actions (clipboard + layout/image export), routed through DataExportController.
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "DataExportController.hpp"
#include "Notifications.hpp"
#include "../../support/share/shareImage.hpp"    // isShareSheetAvailable — no Share button on Linux
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
    actDownloadJson = newAction("Export Layout JSON…", hotkey("downloadJson", "Ctrl+Shift+J"));
    actUploadJson = newAction("Import Layout JSON…", hotkey("uploadJson", "Ctrl+Shift+U"));
    actScript = newAction("Stencil Script…", hotkey("openScript", "Alt+Shift+S"));
    actSaveProjectFile = newAction("Save Project As… (.stencil)", hotkey("saveProject", "Ctrl+Shift+S"));
    actOpenProjectFile = newAction("Open Project… (.stencil)", hotkey("openProject", "Ctrl+Shift+F"));
    // Both must carry the registry's combos, or the Shortcuts window lists chords that do nothing.
    actStencilLiveSync = newAction("Live Sync with File", hotkey("toggleLiveSync", "Ctrl+Shift+Y"));
    actStencilLiveSync->setCheckable(true);
    actStencilLiveSync->setEnabled(false);   // enabled once the project is linked to a .stencil file
    setActionTip(actStencilLiveSync, "Live sync this project to its .stencil file (auto-save + watch for changes)");
    actDeleteProjectFile = newAction("Delete Project File (.stencil)", hotkey("deleteProject", "Ctrl+Shift+Backspace"));
    actDeleteProjectFile->setEnabled(false);   // enabled once the project is linked to a .stencil file
    setActionTip(actDeleteProjectFile, "Delete the linked .stencil file from disk (the project stays open here)");
    actCopyLayout = newAction("Copy Layout JSON", hotkey("copyLayout", "Ctrl+Shift+Alt+C"));
    actPasteLayout = newAction("Paste Layout JSON", QString());
    // actSaveImage owns Ctrl+Shift+D: always "Current". actSaveImageSplit is hidden until a split compare view is active;
    // syncSplitCopyDownloadSlot() moves the shortcut onto it then, so it gets no initial shortcut here.
    actSaveImage = newAction("Current (Tint + Lines/Points)", hotkey("saveImage", "Ctrl+Shift+D"));
    actSaveImageSplit = newAction("With Compare", QString());
    actSaveImageSplit->setVisible(false);
    actSaveImageOriginal = newAction("Original (No Tint, No Lines/Points)", hotkey("saveImageOriginal", "Ctrl+Alt+D"));
    // "Filter Only" renders identical to "Original" with no filter — hidden until one is active.
    actSaveImageTint = newAction("Filter Only (No Lines/Points)", hotkey("saveImageTint", "Ctrl+Shift+Alt+D"));
    actSaveImageTint->setVisible(settings.imageFilter != QLatin1String("none"));
    // actCopyImage owns Ctrl+C — mirrors actSaveImage above.
    actCopyImage = newAction("Current (Tint + Lines/Points)", hotkey("copyImage", "Ctrl+C"));
    actCopyImageSplit = newAction("With Compare", QString());
    actCopyImageSplit->setVisible(false);
    actCopyImageOriginal = newAction("Original (No Tint, No Lines/Points)", hotkey("copyImageOriginal", "Ctrl+Shift+C"));
    actCopyImageTint = newAction("Filter Only (No Lines/Points)", hotkey("copyImageTint", "Ctrl+Alt+C"));
    actCopyImageTint->setVisible(settings.imageFilter != QLatin1String("none"));
    // "Current"'s OWN row, separate so it can hide without hiding the toolbar button; its TEXT mirrors the live combo. Hidden until there are lines/points.
    actSaveImageCurrentRow = newAction("Current (Tint + Lines/Points)", QString());
    actSaveImageCurrentRow->setVisible(false);
    actCopyImageCurrentRow = newAction("Current (Tint + Lines/Points)", QString());
    actCopyImageCurrentRow->setVisible(false);
    actShareImage = newAction("Share Image…", hotkey("shareImage", "Ctrl+Alt+S"));
    // Hidden where the OS has no share sheet (Linux); browser: utils.js supportsShareFiles().
    actShareImage->setVisible(support::isShareSheetAvailable());
    // Ctrl+V: image over layout JSON text (drawingApp.js :563-591); pasteImage() dispatches.
    actPasteImage = newAction("Paste (Image or Layout)", hotkey("paste", "Ctrl+V"));

    connect(actDownloadJson, &QAction::triggered, this, [this] { dataExport->downloadLayout(); });
    connect(actUploadJson, &QAction::triggered, this, [this] { dataExport->uploadLayout(); });
    connect(actScript, &QAction::triggered, this, [this] { openScript(); });
    connect(actSaveProjectFile, &QAction::triggered, this, [this] { saveProjectFileAs(); });
    connect(actStencilLiveSync, &QAction::toggled, this, [this](bool on) { toggleStencilLiveSync(on); });
    connect(actDeleteProjectFile, &QAction::triggered, this, [this] { deleteProjectFile(); });
    connect(actOpenProjectFile, &QAction::triggered, this, [this] {
      const QString path = QFileDialog::getOpenFileName(
          this, "Open project", QString(), "Stencil project (*.stencil)");
      if (!path.isEmpty()) openProjectFile(path);
    });
    connect(actCopyLayout, &QAction::triggered, this, [this] { dataExport->copyLayout(); });
    connect(actPasteLayout, &QAction::triggered, this, [this] { dataExport->pasteLayout(); });
    connect(actSaveImage, &QAction::triggered, this, [this] { dataExport->saveImageFile("current"); });
    connect(actSaveImageCurrentRow, &QAction::triggered, this, [this] { dataExport->saveImageFile("current"); });
    connect(actSaveImageSplit, &QAction::triggered, this, [this] { dataExport->saveImageFile("split"); });
    connect(actSaveImageOriginal, &QAction::triggered, this, [this] { dataExport->saveImageFile("original"); });
    connect(actSaveImageTint, &QAction::triggered, this, [this] { dataExport->saveImageFile("tint"); });
    // The button itself, not the window — DataExportController.hpp shareImage().
    connect(actShareImage, &QAction::triggered, this,
            [this] { dataExport->shareImage(buttonForAction(actShareImage)); });
    // Text selection under the focus first (browser parity); the image is the fallback.
    connect(actCopyImage, &QAction::triggered, this, [this] {
      if (copyFocusedSelection()) return;
      dataExport->copyImageToClipboard("current");
    });
    connect(actCopyImageCurrentRow, &QAction::triggered, this, [this] {
      if (copyFocusedSelection()) return;
      dataExport->copyImageToClipboard("current");
    });
    connect(actCopyImageSplit, &QAction::triggered, this, [this] {
      if (copyFocusedSelection()) return;
      dataExport->copyImageToClipboard("split");
    });
    connect(actCopyImageOriginal, &QAction::triggered, this, [this] {
      if (copyFocusedSelection()) return;
      dataExport->copyImageToClipboard("original");
    });
    connect(actCopyImageTint, &QAction::triggered, this, [this] {
      if (copyFocusedSelection()) return;
      dataExport->copyImageToClipboard("tint");
    });
    connect(actPasteImage, &QAction::triggered, this, &MainWindow::pasteImage);

  }

}  // namespace stencil::gui
