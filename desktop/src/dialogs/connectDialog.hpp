#pragma once
// Connect dialog (mirrors browser connectModal.js): connect to one or more
// collaboration servers, list them, and disconnect. Backed by the window's
// net::ConnectionManager, so the same connections drive shared-project access.
#include <QDialog>
#include <QSet>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QLineEdit;
class QListWidget;
class QWidget;

namespace stencil::net {
  class ConnectionManager;
}

namespace stencil::gui {

  class ListFilterFade;

  class ConnectDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ConnectDialog(stencil::net::ConnectionManager* manager, QWidget* parent = nullptr);
    // Close-early finalize: pending removal slots collapse at once (the dust dies with
    // the dialog), so nothing stale survives into a later show.
    void done(int r) override;
    // "Sync changes to server" lives here beside Auto-connect (browser connectModal.js
    // #connect-sync). The setting itself is MainWindow's (Settings.syncToServer), so the
    // window seeds the box and hears every toggle.
    void setSyncToServer(bool on);

   signals:
    void syncToServerToggled(bool on);
    // Failures are reported the way the browser reports them: a toast on the app's stack
    // (connectModal.js notify(..., 'fail')), not a native alert box. MainWindow owns the
    // stack, so the dialog just says what happened.
    void toast(const QString& text, bool failed);

   protected:
    // Watches the list viewport: rows are re-capped to its width on resize.
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void rebuildList();
    // The row cards' QSS (projects-row look + the gold/amber connection states),
    // set once on the list so it cascades to every row widget.
    QString rowStyleSheet() const;
    // Width a row slot may take: the viewport minus the list's spacing on both sides.
    int rowWidth() const;
    // Fade rows toward the list's edges as it scrolls, instead of cutting one
    // mid-outline (the widget twin of the projects delegate's reveal dissolve).
    void applyRowReveal();
    // Fade + collapse the rows the All / Admin / Non-admin picker excludes (view state
    // only — never persisted) and show the "nothing matches" line when none survive.
    void applyKindFilter();
    // Lazily build the filter transition and its per-row writers (support/filterFade).
    ListFilterFade* filterFade();
    // Sign in again on a row whose session expired: fresh session first, then a
    // token prompt (session OR admin token) if the server refuses.
    void reauthenticate(const QString& url);
    // Scatter the given server rows before they go, matching the projects list and the chat
    // cards — a painted list row has no widget of its own, so its RECT comes apart.
    void scatterRows(const QStringList& urls);
    void doConnect();
    // Show/hide the batch toolbar + update its count from the current selection.
    void updateBatchBar();
    // Select all's pool: the urls whose rows the kind filter leaves on view.
    QStringList shownUrls() const;
    bool allShownSelected() const;
    // Select every row on view / clear the whole selection (browser connect-select-all).
    void toggleSelectAll();
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
    QCheckBox* syncToServer_ = nullptr;
    // "Show:" All / Admin / Non-admin — a view filter over the rows, not a setting.
    QComboBox* kindFilter_ = nullptr;
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
    QPushButton* selectAllBtn_ = nullptr;  // Select all / Deselect all over the filtered view
    // The selection-only actions, in ONE group so the bar's reveal is a single flight
    // (projectsDialog's batchSelectedGroup_ / browser .connect-batch-actions).
    QWidget* batchSelectedGroup_ = nullptr;
    // The kind picker's enter/exit transition (owned by the list).
    ListFilterFade* filterFade_ = nullptr;
  };

}  // namespace stencil::gui
