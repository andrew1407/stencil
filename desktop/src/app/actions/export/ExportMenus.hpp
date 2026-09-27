#pragma once
#include <QImage>

class QAction;
class QMenu;

namespace stencil::gui {

  class MainWindow;

  // The Save/Copy Image option menus: their variant rows (current, with compare, original, filter
  // only), the hover preview of each, and the popups off the toolbar's split buttons.
  class ExportMenus {
   public:
    explicit ExportMenus(MainWindow& w) : w(w) {}

    // After buildToolbar() — needs buttonForAction.
    void wireExportOptionsPopups();
    void wireExportPreviewHover(QMenu* menu);
    void populateExportVariantMenu(QMenu* menu, bool copy);
    QImage exportVariantPreviewImage(QAction* act) const;

    // Must run after applyTheme too — styleActionIcons() resets the split action's icon.
    void syncSplitCopyDownloadSlot();
    void syncExportActions();

   private:
    MainWindow& w;
  };

}  // namespace stencil::gui
