#pragma once
#include "fileStore.hpp"
#include "serverClient.hpp"
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
// newName() to apply the choice. When a ConnectionManager is supplied, server
// (shared) projects are listed alongside the local ones with a golden outline and
// a server marker, refreshed live on a short timer (the desktop analogue of the
// browser modal's WebSocket project-event feed → periodic listProjects refresh).
namespace stencil::gui {

  class ProjectsDialog : public QDialog {
    Q_OBJECT
   public:
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
    // Batch* act on the checked rows (read batchItems()): BatchRemove (any), BatchMoveToServer
    //   / BatchCopyToServer (local-only checked + selectedServerUrl()), BatchMoveToLocal /
    //   BatchCopyToLocal (server-only checked).
    // SetColor: set (or clear) a project's accent colour — read selectedId() +
    //   selectedServerUrl() (empty = local) + selectedColor() ("" = theme default).
    enum class Action { None, Open, OpenInNewWindow, Delete, New, Rename, Expiration, NewBlank,
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
    explicit ProjectsDialog(const std::vector<Project>& projects, long long now,
                            stencil::net::ConnectionManager* connections = nullptr,
                            const QHash<QString, QPixmap>& thumbs = {},
                            QWidget* parent = nullptr);

    Action action() const { return action_; }
    QString selectedId() const { return selectedId_; }
    QString selectedServerUrl() const { return selectedServerUrl_; }
    QString newName() const { return newName_; }
    // For SetColor: the chosen colour ("#rrggbb"), or "" to clear to the theme default.
    QString selectedColor() const { return selectedColor_; }
    // For Batch* actions: the checked rows as (id, serverUrl) pairs (serverUrl empty = local).
    const QVector<QPair<QString, QString>>& batchItems() const { return batchItems_; }

   protected:
    // Hover-magnify: watch the list viewport so hovering a row's thumbnail pops a
    // larger floating preview that follows the cursor.
    bool eventFilter(QObject* obj, QEvent* ev) override;

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
    void createNew();
    void createBlank();

    std::vector<Project> projects_;
    long long now_ = 0;
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
    QComboBox* filter_ = nullptr;   // All / Local / Server / per-server row filter
    QLineEdit* search_ = nullptr;   // name search box (mirrors the browser modal)
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
    QVector<QPair<QString, QString>> batchItems_;
    Action action_ = Action::None;
    QString selectedId_;
    QString selectedServerUrl_;
    QString newName_;
    QString selectedColor_;
  };

}
