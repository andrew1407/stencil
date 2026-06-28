#pragma once
#include "fileStore.hpp"
#include "serverClient.hpp"
#include <QDialog>
#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <vector>

class QListWidget;
class QTimer;
class QNetworkAccessManager;
class QLabel;

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
    enum class Action { None, Open, OpenInNewWindow, Delete, New, Rename, Renew, NewBlank,
                        OpenRemote, MoveToServer, MoveToLocal, MakeLocalCopy };

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
    // Fetch the project `source` image URL ASYNCHRONOUSLY (no blocking): the row shows
    // a placeholder immediately and its icon is swapped in when the download finishes.
    // `key` is the remoteThumbs_ cache key; the result (even a miss) is cached.
    void fetchSourceThumbAsync(const QString& key, const stencil::net::ServerProject& sp);
    // A uniform 56×56 fallback tile (centered native glyph) shown when a row has no
    // image, so every row is the same height. `remote` picks a network vs file glyph.
    QPixmap placeholderIcon(bool remote) const;
    void openSelected();
    void openSelectedInNewWindow();
    void deleteSelected();
    // Move the selected LOCAL project to a server (pick one if several connected).
    void moveToServerSelected();
    // Move the selected SERVER project into local storage.
    void moveToLocalSelected();
    // Make a detached local copy of the selected SERVER project (server copy kept).
    void makeLocalCopySelected();
    void renameSelected();
    void renewSelected();
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
    Action action_ = Action::None;
    QString selectedId_;
    QString selectedServerUrl_;
    QString newName_;
  };

}
