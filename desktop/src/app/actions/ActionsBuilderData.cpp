// MainWindow's data actions (clipboard + layout/image export), routed through DataExportController.
#include "MainWindow.hpp"
#include "ActionsBuilder.hpp"
#include "DataExportController.hpp"
#include "StencilFileSync.hpp"
#include "../../support/share/shareImage.hpp"    // isShareSheetAvailable — no Share button on Linux
#include <QClipboard>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>

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

  void ActionsBuilder::createDataActions() {
    // Clipboard hotkeys come from hotkeysConfig.json so a rebind re-applies live; the file export/import are menu-only.
    w.acts.downloadJson = newAction("Export Layout JSON…", w.keys.value("downloadJson", "Ctrl+Shift+J"));
    w.acts.uploadJson = newAction("Import Layout JSON…", w.keys.value("uploadJson", "Ctrl+Shift+U"));
    w.acts.script = newAction("Stencil Script…", w.keys.value("openScript", "Alt+Shift+S"));
    w.acts.saveProjectFile = newAction("Save Project As… (.stencil)", w.keys.value("saveProject", "Ctrl+Shift+S"));
    w.acts.openProjectFile = newAction("Open Project… (.stencil)", w.keys.value("openProject", "Ctrl+Shift+F"));
    // Both must carry the registry's combos, or the Shortcuts window lists chords that do nothing.
    w.acts.stencilLiveSync = newAction("Live Sync with File", w.keys.value("toggleLiveSync", "Ctrl+Shift+Y"));
    w.acts.stencilLiveSync->setCheckable(true);
    w.acts.stencilLiveSync->setEnabled(false);   // enabled once the project is linked to a .stencil file
    w.setActionTip(w.acts.stencilLiveSync, "Live sync this project to its .stencil file (auto-save + watch for changes)");
    w.acts.deleteProjectFile = newAction("Delete Project File (.stencil)", w.keys.value("deleteProject", "Ctrl+Shift+Backspace"));
    w.acts.deleteProjectFile->setEnabled(false);   // enabled once the project is linked to a .stencil file
    w.setActionTip(w.acts.deleteProjectFile, "Delete the linked .stencil file from disk (the project stays open here)");
    w.acts.copyLayout = newAction("Copy Layout JSON", w.keys.value("copyLayout", "Ctrl+Shift+Alt+C"));
    w.acts.pasteLayout = newAction("Paste Layout JSON", QString());
    // acts.saveImage owns Ctrl+Shift+D: always "Current". acts.saveImageSplit is hidden until a split compare view is active;
    // syncSplitCopyDownloadSlot() moves the shortcut onto it then, so it gets no initial shortcut here.
    w.acts.saveImage = newAction("Current (Tint + Lines/Points)", w.keys.value("saveImage", "Ctrl+Shift+D"));
    w.acts.saveImageSplit = newAction("With Compare", QString());
    w.acts.saveImageSplit->setVisible(false);
    w.acts.saveImageOriginal = newAction("Original (No Tint, No Lines/Points)", w.keys.value("saveImageOriginal", "Ctrl+Alt+D"));
    // "Filter Only" renders identical to "Original" with no filter — hidden until one is active.
    w.acts.saveImageTint = newAction("Filter Only (No Lines/Points)", w.keys.value("saveImageTint", "Ctrl+Shift+Alt+D"));
    w.acts.saveImageTint->setVisible(w.settings.imageFilter != QLatin1String("none"));
    // acts.copyImage owns Ctrl+C — mirrors acts.saveImage above.
    w.acts.copyImage = newAction("Current (Tint + Lines/Points)", w.keys.value("copyImage", "Ctrl+C"));
    w.acts.copyImageSplit = newAction("With Compare", QString());
    w.acts.copyImageSplit->setVisible(false);
    w.acts.copyImageOriginal = newAction("Original (No Tint, No Lines/Points)", w.keys.value("copyImageOriginal", "Ctrl+Shift+C"));
    w.acts.copyImageTint = newAction("Filter Only (No Lines/Points)", w.keys.value("copyImageTint", "Ctrl+Alt+C"));
    w.acts.copyImageTint->setVisible(w.settings.imageFilter != QLatin1String("none"));
    // "Current"'s OWN row, separate so it can hide without hiding the toolbar button; its TEXT mirrors the live combo. Hidden until there are lines/points.
    w.acts.saveImageCurrentRow = newAction("Current (Tint + Lines/Points)", QString());
    w.acts.saveImageCurrentRow->setVisible(false);
    w.acts.copyImageCurrentRow = newAction("Current (Tint + Lines/Points)", QString());
    w.acts.copyImageCurrentRow->setVisible(false);
    w.acts.shareImage = newAction("Share Image…", w.keys.value("shareImage", "Ctrl+Alt+S"));
    // Hidden where the OS has no share sheet (Linux); browser: utils.js supportsShareFiles().
    w.acts.shareImage->setVisible(support::isShareSheetAvailable());
    // Ctrl+V: image over layout JSON text (drawingApp.js :563-591); pasteImage() dispatches.
    w.acts.pasteImage = newAction("Paste (Image or Layout)", w.keys.value("paste", "Ctrl+V"));

    QObject::connect(w.acts.downloadJson, &QAction::triggered, &w, [this] { w.dataExport->downloadLayout(); });
    QObject::connect(w.acts.uploadJson, &QAction::triggered, &w, [this] { w.dataExport->uploadLayout(); });
    QObject::connect(w.acts.script, &QAction::triggered, &w, [this] { w.parts.scriptHost.openScript(); });
    QObject::connect(w.acts.saveProjectFile, &QAction::triggered, &w, [this] { w.parts.persistence.saveProjectFileAs(); });
    QObject::connect(w.acts.stencilLiveSync, &QAction::toggled, &w, [this](bool on) { w.stencilSync->setLiveSync(on); });
    QObject::connect(w.acts.deleteProjectFile, &QAction::triggered, &w, [this] { w.parts.persistence.deleteProjectFile(); });
    QObject::connect(w.acts.openProjectFile, &QAction::triggered, &w, [this] {
      const QString path = QFileDialog::getOpenFileName(
          &w, "Open project", QString(), "Stencil project (*.stencil)");
      if (!path.isEmpty()) w.parts.sourceOpener.openProjectFile(path);
    });
    QObject::connect(w.acts.copyLayout, &QAction::triggered, &w, [this] { w.dataExport->copyLayout(); });
    QObject::connect(w.acts.pasteLayout, &QAction::triggered, &w, [this] { w.dataExport->pasteLayout(); });
    QObject::connect(w.acts.saveImage, &QAction::triggered, &w, [this] { w.dataExport->saveImageFile("current"); });
    QObject::connect(w.acts.saveImageCurrentRow, &QAction::triggered, &w, [this] { w.dataExport->saveImageFile("current"); });
    QObject::connect(w.acts.saveImageSplit, &QAction::triggered, &w, [this] { w.dataExport->saveImageFile("split"); });
    QObject::connect(w.acts.saveImageOriginal, &QAction::triggered, &w, [this] { w.dataExport->saveImageFile("original"); });
    QObject::connect(w.acts.saveImageTint, &QAction::triggered, &w, [this] { w.dataExport->saveImageFile("tint"); });
    // The button itself, not the window — DataExportController.hpp shareImage().
    QObject::connect(w.acts.shareImage, &QAction::triggered, &w,
                     [this] { w.dataExport->shareImage(w.buttonForAction(w.acts.shareImage)); });
    // Text selection under the focus first (browser parity); the image is the fallback.
    QObject::connect(w.acts.copyImage, &QAction::triggered, &w, [this] {
      if (copyFocusedSelection()) return;
      w.dataExport->copyImageToClipboard("current");
    });
    QObject::connect(w.acts.copyImageCurrentRow, &QAction::triggered, &w, [this] {
      if (copyFocusedSelection()) return;
      w.dataExport->copyImageToClipboard("current");
    });
    QObject::connect(w.acts.copyImageSplit, &QAction::triggered, &w, [this] {
      if (copyFocusedSelection()) return;
      w.dataExport->copyImageToClipboard("split");
    });
    QObject::connect(w.acts.copyImageOriginal, &QAction::triggered, &w, [this] {
      if (copyFocusedSelection()) return;
      w.dataExport->copyImageToClipboard("original");
    });
    QObject::connect(w.acts.copyImageTint, &QAction::triggered, &w, [this] {
      if (copyFocusedSelection()) return;
      w.dataExport->copyImageToClipboard("tint");
    });
    QObject::connect(w.acts.pasteImage, &QAction::triggered, &w, [this] { w.parts.sourceOpener.pasteImage(); });

  }

}  // namespace stencil::gui
