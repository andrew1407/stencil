#pragma once
#include "fileStore.hpp"
#include "serverClient.hpp"
#include "tooltipRows.hpp"  // core::UnitFormat for the tooltip's "Line: <len> <unit>" row
#include <QDialog>
#include <QHash>
#include <QPair>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QVector>
#include <vector>

class QListWidget;
class QListWidgetItem;
class QComboBox;
class QLineEdit;
class QTimer;
class QNetworkAccessManager;
class QLabel;
class QPoint;
class QPushButton;
class QWidget;

namespace stencil::net {
  class ConnectionManager;
}

// Saved-projects browser. Mirrors browser/js/ui/projectsModal.js: list projects,
// open / delete one, or create a new one. exec() then read action()/selectedId()/
// newName() to apply the choice; removals (Delete / batch Remove / Clear All) are
// instead confirmed in-dialog and signalled, so the window stays open (see signals).
// When a ConnectionManager is supplied, server
// (shared) projects are listed alongside the local ones with a golden outline and
// a server badge, refreshed live on a short timer (the desktop analogue of the
// browser modal's WebSocket project-event feed → periodic listProjects refresh).
namespace stencil::gui {

  class ProjectDragZones;

  // Which batch-transfer directions apply to a selection of `locals` local + `remotes`
  // server rows. Inapplicable directions are HIDDEN, not greyed (browser parity:
  // projectsModal.js updateBatchBar). toServer covers move+copy to server; toLocal
  // covers move+copy to local.
  struct BatchDirections { bool toServer = false; bool toLocal = false; };
  BatchDirections batchDirectionsFor(int locals, int remotes, bool haveServers);

  class ProjectsDialog : public QDialog {
    Q_OBJECT
   public:
    // Supply the main window's drag-out zone overlay (open here / new window / remove), shown
    // while a project row is dragged out of this (modal) dialog. Optional (nullptr = no zones).
    void setDragZones(ProjectDragZones* z) { dragZones_ = z; }

    // NewBlank: create a blank solid-color image (the main window opens its
    // BlankImageDialog after this dialog closes).
    // OpenInNewWindow: like Open, but the main window loads the project into a
    // fresh top-level window instead of replacing the current canvas.
    // OpenRemote: open a server-stored project (read selectedServerUrl()+selectedId()).
    // MoveToServer: store a LOCAL project on a server, then drop the local copy
    //   (read selectedId() + selectedServerUrl()).
    // MoveToLocal: copy a SERVER project into local storage, then delete it from the
    //   server (read selectedServerUrl() + selectedId()).
    // MakeLocalCopy: copy a SERVER project into local storage (named "<name>-local")
    //   and open it, leaving the server copy in place (read selectedServerUrl()+selectedId()).
    // CopyToServer: copy a LOCAL project to a server, leaving the local one in place
    //   (read selectedId() + selectedServerUrl() + newName()).
    // MakeLocalCopy now carries newName() (the copy's name, default "<name>-copy").
    // Batch* act on the checked rows (read batchItems()): BatchMoveToServer
    //   / BatchCopyToServer (local-only checked + selectedServerUrl()), BatchMoveToLocal /
    //   BatchCopyToLocal (server-only checked). BatchRemove never reaches action() —
    //   it confirms in-dialog and emits removeRequested (kept as runBatch's dispatch tag).
    // SetColor: set (or clear) a project's accent colour — read selectedId() +
    //   selectedServerUrl() (empty = local) + selectedColor() ("" = theme default).
    enum class Action { None, Open, OpenInNewWindow, New, Rename, Expiration, NewBlank,
                        OpenRemote, MoveToServer, MoveToLocal, MakeLocalCopy, CopyToServer,
                        SetColor,
                        BatchRemove, BatchMoveToServer, BatchCopyToServer,
                        BatchMoveToLocal, BatchCopyToLocal, ClearAll };

    // `now` (epoch ms) is the reference point for the per-row expiry labels and
    // their warning/expired colouring; the caller passes its clock so the dialog
    // stays free of time sources. `connections` (nullable) supplies the shared
    // server projects shown with a golden outline. `thumbs` maps a local project
    // id to its pre-rendered EDITED-result preview (filtered image + drawn lines),
    // shown as the row icon; the caller renders them via the canvas/export path.
    // `unit` is the active display unit (cm/inches); it converts each local project's
    // cached lineLengthCm into a "Line: <len> <unit>" row in the per-row tooltip.
    explicit ProjectsDialog(const std::vector<Project>& projects, long long now,
                            stencil::net::ConnectionManager* connections = nullptr,
                            const QHash<QString, QPixmap>& thumbs = {},
                            core::UnitFormat unit = {},
                            QWidget* parent = nullptr);

    Action action() const { return action_; }
    QString selectedId() const { return selectedId_; }
    // False when the user's gesture already expressed intent unambiguously (a
    // double click): MainWindow then skips its "Open this project?" prompt.
    // True for a single click, Return, and the drag-out zones.
    bool confirmRequested() const { return confirmOpen_; }
    QString selectedServerUrl() const { return selectedServerUrl_; }
    QString newName() const { return newName_; }
    // For SetColor: the chosen colour ("#rrggbb"), or "" to clear to the theme default.
    QString selectedColor() const { return selectedColor_; }
    // For Batch* actions: the checked rows as (id, serverUrl) pairs (serverUrl empty = local).
    const QVector<QPair<QString, QString>>& batchItems() const { return batchItems_; }

    // Replace the listed projects and repaint — the owner calls this after acting on a
    // request signalled below, so the dialog STAYS OPEN and simply shows the new state.
    void setProjects(const std::vector<Project>& projects);

   signals:
    // "Clear All (Local)": already confirmed INSIDE the dialog, so the confirmation sits
    // over the still-open window rather than replacing it. The owner does the removal and
    // calls setProjects().
    void clearAllRequested();
    // Remove (single ⋯/right-click Delete, drag-out Remove zone, batch Remove): same
    // stay-open pattern — already confirmed in-dialog, the doomed rows are scattering.
    // Items are (id, serverUrl) pairs (serverUrl empty = local); the owner removes them
    // and calls setProjects().
    void removeRequested(const QVector<QPair<QString, QString>>& items);

   protected:
    // Hover-magnify: watch the list viewport so hovering a row's thumbnail pops a
    // larger floating preview that follows the cursor.
    bool eventFilter(QObject* obj, QEvent* ev) override;
    // Finalize pending row retirements (and stop their scatters) BEFORE the close
    // flight photographs the dialog — a removed row must never resurface in the
    // shrinking ghost, however early the dialog is closed.
    void done(int result) override;

   private:
    void refresh();
    // Re-list server projects across every connection and append golden rows.
    void refreshRemote();
    // The edited preview for a server project: its rendered `result` (or the
    // `original` if never saved), fetched via the connection and cached by version.
    // When the server holds no stored bytes (e.g. an extension-added project that
    // only recorded the image's web URL), falls back to fetching that `source` URL.
    QPixmap remoteThumb(const stencil::net::ServerProject& sp);
    // Fetch a server project's stored preview ASYNCHRONOUSLY: downloadFile("result") →
    // downloadFile("original") → the `source` web URL, swapping the row icon in on arrival so the
    // dialog never blocks on the network. `key` is the remoteThumbs_ cache key.
    void fetchServerThumbAsync(const QString& key, const stencil::net::ServerProject& sp);
    // Fetch the project `source` image URL ASYNCHRONOUSLY (no blocking): the row shows
    // a placeholder immediately and its icon is swapped in when the download finishes.
    // `key` is the remoteThumbs_ cache key; the result (even a miss) is cached.
    void fetchSourceThumbAsync(const QString& key, const stencil::net::ServerProject& sp);
    // Cache `img` (scaled) as `key`'s thumb and swap the matching live row's placeholder icon.
    // Shared tail of the server-download and source-URL fetch paths. An empty `img` caches a miss.
    void applyRemoteThumb(const QString& key, const QString& id, const QString& serverUrl,
                          const QImage& img);
    // A uniform 56×56 fallback tile (centered native glyph) shown when a row has no
    // image, so every row is the same height. `remote` picks a network vs file glyph.
    QPixmap placeholderIcon(bool remote) const;
    // Per-row action menu (the "⋯" kebab + right-click both call this). Selects
    // `it` first, since the action slots act on the current item.
    void showRowMenu(QListWidgetItem* it, const QPoint& globalPos);
    void openSelected();
    void openSelectedInNewWindow();
    // ── row-open gestures (browser parity) ──
    //   single click            → confirm, then open in the CURRENT window
    //   double click            → open immediately, no confirmation
    //   Ctrl/⌘ + single click   → confirm, then open in a NEW window
    //   Ctrl/⌘ + double click   → new window immediately, no confirmation
    //   Return on a focused row → treated as a plain single click (confirms)
    // The dialog only records the choice; MainWindow shows the confirmation
    // AFTER exec() returns (a QMessageBox raised from inside the click would be
    // dismissed by the same release), gated on the public confirmRequested().
    //
    // Arm the deferred single-click open. Deferring by doubleClickInterval() is
    // the crux: a double click must cancel it, or the confirmation flashes up
    // before the second click lands.
    void scheduleRowOpen(QListWidgetItem* it);
    void fireRowOpen();       // the timer expired → a genuine single click
    void openRow(QListWidgetItem* it, bool newWindow, bool confirm);
    void deleteSelected();
    // Move the selected LOCAL project to a server (pick one if several connected).
    void moveToServerSelected();
    // Copy the selected LOCAL project to a server (local copy kept), prompting a name.
    void copyToServerSelected();
    // Move the selected SERVER project into local storage.
    void moveToLocalSelected();
    // Make a detached local copy of the selected SERVER project (server copy kept), prompting a name.
    void makeLocalCopySelected();
    // Re-apply the storage filter (All / Local / Server / a specific server) + the search
    // text to the visible rows.
    void applyFilter();
    // (Re)populate the "Show:" combo with All / Local / All-servers + one entry per connected
    // server, preserving the current selection. Called when the connected-server set changes.
    void rebuildFilterOptions();
    // Multi-select: collect the checked rows + show/enable the batch toolbar; run a batch action.
    void onItemChanged(QListWidgetItem* it);
    void updateBatchBar();
    // Select-all toggle over the CURRENT filtered view (browser: the per-render
    // `selectables` pool + updateSelectAll).
    void updateSelectAll();
    bool allFilteredChecked() const;
    void toggleSelectAll();
    void runBatch(Action act);
    void renameSelected();
    void expirationSelected();
    // Pop a colour picker (seeded with the row's current colour) and emit SetColor.
    void setColorSelected();
    // Clear the row's colour back to the theme default (emit SetColor with "").
    void clearColorSelected();
    // Resolve `it`'s (id, serverUrl), set the SetColor result fields, and accept().
    void emitSetColor(QListWidgetItem* it, const QString& color);
    // The selected row's current colour ("#rrggbb" or "") — local meta or server record.
    QString currentRowColor() const;
    // The (serverUrl|id) key for list row `i` (matches checked_ keys); "" for placeholder rows.
    QString rowKeyAt(int i) const;
    // Scatter the given rows (empty = every data row) before they leave, the way the single
    // Remove already does — a painted list row has no widget, so its RECT comes apart.
    void scatterRows(const QSet<QString>& keys = {});
    // Blank `it` the instant its scatter starts (the slot stays open), then drop the
    // item once the animation has played — the overlay flies a snapshot, so leaving
    // the real row painted underneath hid the removal entirely.
    void retireRow(QListWidgetItem* it);
    void createNew();
    void createBlank();

    std::vector<Project> projects_;
    long long now_ = 0;
    core::UnitFormat unit_;  // active display unit for the tooltip's line-length row
    stencil::net::ConnectionManager* connections_ = nullptr;
    // id -> pre-rendered local-project preview (edited result), shown as the row icon.
    QHash<QString, QPixmap> thumbs_;
    // Cached server-project previews, keyed "serverUrl|id|version" so the periodic
    // remote re-list reuses them instead of re-downloading unchanged projects.
    QHash<QString, QPixmap> remoteThumbs_;
    QVector<stencil::net::ServerProject> remote_;
    // Lazily-created network manager for fetching server projects' `source` image
    // URLs when the server itself holds no stored bytes.
    QNetworkAccessManager* thumbNet_ = nullptr;
    // Cache keys with an in-flight async source fetch, so a re-list doesn't kick off
    // a duplicate download for the same project.
    QSet<QString> thumbInFlight_;
    // Frameless floating label showing the magnified thumbnail under the cursor.
    QLabel* hoverPreview_ = nullptr;
    QTimer* remoteTimer_ = nullptr;
    bool remoteBusy_ = false;
    // False until the first server listing resolves — drives the "Loading shared
    // projects…" placeholder so the dialog can open instantly (remote fetch deferred).
    bool remoteLoaded_ = false;
    QListWidget* list_ = nullptr;
    // "Clear All" (local-only) button; relabelled "Clear All Local" live while a server
    // is connected so the label tracks the actual removal across the remote poll.
    QPushButton* clearAllBtn_ = nullptr;
    QComboBox* filter_ = nullptr;   // All / Local / Server / per-server row filter
    ProjectDragZones* dragZones_ = nullptr;  // main-window drag-out overlay (nullptr = none)
    QComboBox* sortCombo_ = nullptr;  // Name / Local first / Server first / Newest / Oldest / Manual
    QComboBox* searchModeCombo_ = nullptr;  // Name + keywords / Names only / Keywords only
    QLineEdit* search_ = nullptr;   // name search box (mirrors the browser modal)
    QPushButton* selectAllBtn_ = nullptr;  // Select all / Deselect all over the filtered view
    QStringList knownServerUrls_;   // last server set the filter combo was built from
    // Multi-select: checked row keys ("serverUrl|id"; serverUrl empty = local), the batch
    // toolbar + its buttons, and the resolved (id, serverUrl) pairs for the chosen batch action.
    QSet<QString> checked_;
    QWidget* batchBar_ = nullptr;
    QLabel* batchCount_ = nullptr;
    QPushButton* batchToServer_ = nullptr;
    QPushButton* batchCopyServer_ = nullptr;
    QPushButton* batchToLocal_ = nullptr;
    QPushButton* batchCopyLocal_ = nullptr;
    bool building_ = false;   // suppress itemChanged while refresh() sets check states
    // Row-open gesture state (see confirmRequested()).
    QTimer* clickTimer_ = nullptr;      // pending single-click open
    int pendingRow_ = -1;               // row it applies to
    bool pendingNewWindow_ = false;     // Ctrl/⌘ was down for that click
    Qt::KeyboardModifiers pressMods_;   // modifiers of the last press on the list
    bool pressOnCheck_ = false;         // last press landed on a row's checkbox
    bool confirmOpen_ = true;           // single click / drag-out ask; double click doesn't
    bool rowDragging_ = false;          // a drag must not open anything on release
    QVector<QPair<QString, QString>> batchItems_;
    Action action_ = Action::None;
    QString selectedId_;
    QString selectedServerUrl_;
    QString newName_;
    QString selectedColor_;
  };

}
