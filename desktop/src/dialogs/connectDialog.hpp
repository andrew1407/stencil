#pragma once
// Connect dialog (mirrors browser connectModal.js): connect to one or more
// collaboration servers, list them, and disconnect. Backed by the window's
// net::ConnectionManager, so the same connections drive shared-project access.
#include <QDialog>
#include <QSet>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QWidget;

namespace stencil::net {
  class ConnectionManager;
}

namespace stencil::gui {

  class ConnectDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ConnectDialog(stencil::net::ConnectionManager* manager, QWidget* parent = nullptr);
    // Close-early finalize: pending removal slots collapse at once (the dust dies with
    // the dialog), so nothing stale survives into a later show.
    void done(int r) override;

   protected:
    // Watches the list viewport: rows are re-capped to its width on resize.
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void rebuildList();
    // Sign in again on a row whose session expired: fresh session first, then a
    // token prompt (session OR admin token) if the server refuses.
    void reauthenticate(const QString& url);
    // Scatter the given server rows before they go, matching the projects list and the chat
    // cards — a painted list row has no widget of its own, so its RECT comes apart.
    void scatterRows(const QStringList& urls);
    void doConnect();
    // Show/hide the batch toolbar + update its count from the current selection.
    void updateBatchBar();
    // Yes/No confirm, then disconnect + refresh — the single remove path shared by the
    // per-row ✕, the batch Disconnect, and the drag-out-of-the-dialog gesture.
    void confirmDisconnect(const QString& url);

    stencil::net::ConnectionManager* manager_;
    QLineEdit* urlEdit_ = nullptr;
    QLineEdit* tokenEdit_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* reconnectAllBtn_ = nullptr;
    // "Auto-connect on open" — moved here from Settings (it's a connection
    // preference); persisted to net::connectionStore on toggle.
    QCheckBox* autoConnect_ = nullptr;
    // Multi-select: urls checked for a batch reconnect/disconnect, + the toolbar.
    QSet<QString> selected_;
    // Retire-then-finalize (projectsDialog parity): urls whose removal dust is playing.
    // Their blanked rows hold their slots — and rebuildList (so also the empty state)
    // waits — until the dust settles.
    QSet<QString> doomed_;
    // Urls already shown as rows, so rebuildList can gather-in only the NEW ones.
    QSet<QString> known_;
    QWidget* batchBar_ = nullptr;
    QLabel* batchCount_ = nullptr;
  };

}  // namespace stencil::gui
