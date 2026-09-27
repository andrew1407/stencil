// The export-variant rows: their previews, the menu each option popup lists, and keeping them in
// step with the compare state and the split Copy/Download slot.
#include "MainWindow.hpp"
#include "ExportMenus.hpp"
#include "CanvasWidget.hpp"
#include <QMenu>

namespace stencil::gui {

  // The rendered preview for one export-variant QAction; null for any action that isn't one of ours.
  QImage ExportMenus::exportVariantPreviewImage(QAction* act) const {
    struct Spec { QAction* action; const char* variant; };
    const Spec specs[] = {
        {w.acts.copyImage, "current"},
        {w.acts.saveImage, "current"},
        {w.acts.copyImageCurrentRow, "current"},
        {w.acts.saveImageCurrentRow, "current"},
        {w.acts.copyImageSplit, "split"},
        {w.acts.saveImageSplit, "split"},
        {w.acts.copyImageOriginal, "original"},
        {w.acts.copyImageTint, "tint"},
        {w.acts.saveImageOriginal, "original"},
        {w.acts.saveImageTint, "tint"},
    };
    for (const auto& s : specs)
      if (s.action == act) return w.canvas->renderToImage(QString::fromLatin1(s.variant));
    return QImage();
  }

  // One export-variant menu, identical on every surface: "With Compare" (only while comparing), "Current"'s OWN row,
  // then the two fixed variants — Copy lists Filter Only before Original, Download the reverse (browser export/optionsMenu.js).
  void ExportMenus::populateExportVariantMenu(QMenu* menu, bool copy) {
    if (copy) {
      menu->addAction(w.acts.copyImageSplit);
      menu->addAction(w.acts.copyImageCurrentRow);
      menu->addAction(w.acts.copyImageTint);
      menu->addAction(w.acts.copyImageOriginal);
    } else {
      menu->addAction(w.acts.saveImageSplit);
      menu->addAction(w.acts.saveImageCurrentRow);
      menu->addAction(w.acts.saveImageOriginal);
      menu->addAction(w.acts.saveImageTint);
    }
    wireExportPreviewHover(menu);
  }

  // Two enabled actions cannot share a shortcut, so Ctrl+C/Ctrl+Shift+D moves onto the split
  // action while comparing.
  void ExportMenus::syncSplitCopyDownloadSlot() {
    const bool split = w.canvas->isSplitCompare();
    w.acts.copyImageSplit->setVisible(split);
    w.acts.saveImageSplit->setVisible(split);
    // Idempotent: a moved shortcut leaves the source empty.
    auto moveShortcut = [](QAction* from, QAction* to) {
      if (!from->shortcut().isEmpty()) { to->setShortcut(from->shortcut()); from->setShortcut(QKeySequence()); }
    };
    if (split) {
      moveShortcut(w.acts.copyImage, w.acts.copyImageSplit);
      moveShortcut(w.acts.saveImage, w.acts.saveImageSplit);
    } else {
      moveShortcut(w.acts.copyImageSplit, w.acts.copyImage);
      moveShortcut(w.acts.saveImageSplit, w.acts.saveImage);
    }
    // "Current"'s own row carries a manual "\t"+combo hint mirroring the live primary combo
    // (browser: ctx-copy-img-current-hk swap).
    auto setRowHint = [](QAction* row, QAction* primary) {
      const QString combo = primary->shortcut().isEmpty()
          ? QString() : primary->shortcut().toString(QKeySequence::NativeText);
      row->setText(QStringLiteral("Current (Tint + Lines/Points)") +
                   (combo.isEmpty() ? QString() : QStringLiteral("\t") + combo));
    };
    setRowHint(w.acts.copyImageCurrentRow, w.acts.copyImage);
    setRowHint(w.acts.saveImageCurrentRow, w.acts.saveImage);
  }

  // Browser contextMenu.js syncState parity: rows that would render byte-identical to a sibling
  // hide.
  void ExportMenus::syncExportActions() {
    const bool hasImg = w.canvas->hasImage();
    const bool hasLines = !w.canvas->allLines().empty();
    w.acts.copyImage->setEnabled(hasImg);
    w.acts.saveImage->setEnabled(hasImg);
    w.acts.copyImageSplit->setEnabled(hasImg);
    w.acts.saveImageSplit->setEnabled(hasImg);
    w.acts.copyImageCurrentRow->setEnabled(hasImg);
    w.acts.saveImageCurrentRow->setEnabled(hasImg);
    w.acts.copyImageOriginal->setEnabled(hasImg);
    w.acts.copyImageTint->setEnabled(hasImg);
    w.acts.saveImageOriginal->setEnabled(hasImg);
    w.acts.saveImageTint->setEnabled(hasImg);
    const bool hasFilter = w.settings.imageFilter != QLatin1String("none");
    w.acts.copyImageTint->setVisible(hasFilter);
    w.acts.saveImageTint->setVisible(hasFilter);
    w.acts.copyImageCurrentRow->setVisible(hasLines);
    w.acts.saveImageCurrentRow->setVisible(hasLines);
    syncSplitCopyDownloadSlot();
    // Qt will not enable an invisible action.
    w.acts.shareImage->setEnabled(hasImg);
  }
}  // namespace stencil::gui
