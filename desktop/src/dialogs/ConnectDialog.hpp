#pragma once
// Connect dialog (browser connectModal.js), backed by the window's net::ConnectionManager.
#include <QDialog>
#include <QSet>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QKeyEvent;
class QLineEdit;
class QListWidget;
class QHBoxLayout;
class QVBoxLayout;
class QWidget;

namespace stencil::net {
  class ConnectionManager;
  class ServerClient;
}

namespace stencil::gui {

  class ListFilterFade;

  class ConnectDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ConnectDialog(stencil::net::ConnectionManager* manager, QWidget* parent = nullptr);
    // Pending removal slots collapse at once so nothing stale survives into a later show.
    void done(int r) override;
    // Browser connectModal.js #connect-sync; the setting is MainWindow's (Settings.syncToServer).
    void setSyncToServer(bool on);

   signals:
    void syncToServerToggled(bool on);
    // Browser parity: a toast on the app's stack (MainWindow owns it), not a native alert box.
    void toast(const QString& text, bool failed);

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    // Return connects wherever the focus is — a removed row can leave the dialog with no focus widget.
    void keyPressEvent(QKeyEvent* e) override;

   private:
    // Construction order is observable (tab order, findChildren); buildConnectForm hands back the default button.
    QPushButton* buildConnectForm(QVBoxLayout* root);
    void buildConnectionPrefs(QVBoxLayout* root);
    void buildConnectBatchBar(QVBoxLayout* root);
    void buildConnectionList(QVBoxLayout* root);
    void rebuildList();
    // `rowIndex` is the drag-reorder slot.
    void addConnectionRow(const QString& url, int rowIndex);
    void addConnectionRowActions(QHBoxLayout* h, const QString& url,
                                 stencil::net::ServerClient* cl, bool expired, bool admin);
    QString rowStyleSheet() const;
    int rowWidth() const;
    // The widget twin of the projects delegate's reveal dissolve.
    void applyRowReveal();
    // View state only — never persisted.
    void applyKindFilter();
    ListFilterFade* filterFade();
    // Fresh session first, then a token prompt if the server refuses.
    void reauthenticate(const QString& url);
    // A painted list row has no widget of its own, so its RECT comes apart.
    void scatterRows(const QStringList& urls);
    void doConnect();
    void updateBatchBar();
    QStringList shownUrls() const;
    bool allShownSelected() const;
    void toggleSelectAll();
    // The single remove path: per-row ✕, batch Disconnect, and the drag-out gesture.
    void confirmDisconnect(const QString& url);

    stencil::net::ConnectionManager* manager_;
    QLineEdit* urlEdit_ = nullptr;
    QLineEdit* tokenEdit_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* reconnectAllBtn_ = nullptr;
    // A connection preference (net::connectionStore), not a Settings one.
    QCheckBox* autoConnect_ = nullptr;
    QCheckBox* syncToServer_ = nullptr;
    QComboBox* kindFilter_ = nullptr;
    QSet<QString> selected_;
    // Retire-then-finalize (projectsDialog parity): rebuildList waits until the dust settles.
    QSet<QString> doomed_;
    QSet<QString> known_;
    QWidget* batchBar_ = nullptr;
    QLabel* batchCount_ = nullptr;
    QPushButton* selectAllBtn_ = nullptr;
    // ONE group so the bar's reveal is a single flight (browser .connect-batch-actions).
    QWidget* batchSelectedGroup_ = nullptr;
    ListFilterFade* filterFade_ = nullptr;
  };

}  // namespace stencil::gui
