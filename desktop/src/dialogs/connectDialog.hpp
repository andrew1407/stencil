#pragma once
// Connect dialog (mirrors browser connectModal.js): connect to one or more
// collaboration servers, list them, and disconnect. Backed by the window's
// net::ConnectionManager, so the same connections drive shared-project access.
#include <QDialog>

class QCheckBox;
class QLineEdit;
class QListWidget;

namespace stencil::net {
  class ConnectionManager;
}

namespace stencil::gui {

  class ConnectDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ConnectDialog(stencil::net::ConnectionManager* manager, QWidget* parent = nullptr);

   private:
    void rebuildList();
    void doConnect();

    stencil::net::ConnectionManager* manager_;
    QLineEdit* urlEdit_ = nullptr;
    QLineEdit* tokenEdit_ = nullptr;
    QListWidget* list_ = nullptr;
    // "Auto-connect on open" — moved here from Settings (it's a connection
    // preference); persisted to net::connectionStore on toggle.
    QCheckBox* autoConnect_ = nullptr;
  };

}  // namespace stencil::gui
