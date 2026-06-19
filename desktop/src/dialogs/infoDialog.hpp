#pragma once
#include <QDialog>

// Info & Shortcuts dialog. Renders browser/js/config/infoConfig.json (usage tips)
// and hotkeysConfig.json (key bindings), both embedded as Qt resources, exactly
// like the browser info modal (browser/js/ui/infoModal.js).
namespace stencil::gui {

  class InfoDialog : public QDialog {
    Q_OBJECT
   public:
    explicit InfoDialog(QWidget* parent = nullptr);

   private:
    static QString buildHtml();
  };

}
