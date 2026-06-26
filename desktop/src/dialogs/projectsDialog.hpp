#pragma once
#include "fileStore.hpp"
#include "serverClient.hpp"
#include <QDialog>
#include <QString>
#include <vector>

class QListWidget;
class QTimer;

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
    enum class Action { None, Open, OpenInNewWindow, Delete, New, Rename, Renew, NewBlank, OpenRemote };

    // `now` (epoch ms) is the reference point for the per-row expiry labels and
    // their warning/expired colouring; the caller passes its clock so the dialog
    // stays free of time sources. `connections` (nullable) supplies the shared
    // server projects shown with a golden outline.
    explicit ProjectsDialog(const std::vector<Project>& projects, long long now,
                            stencil::net::ConnectionManager* connections = nullptr,
                            QWidget* parent = nullptr);

    Action action() const { return action_; }
    QString selectedId() const { return selectedId_; }
    QString selectedServerUrl() const { return selectedServerUrl_; }
    QString newName() const { return newName_; }

   private:
    void refresh();
    // Re-list server projects across every connection and append golden rows.
    void refreshRemote();
    void openSelected();
    void openSelectedInNewWindow();
    void deleteSelected();
    void renameSelected();
    void renewSelected();
    void createNew();
    void createBlank();

    std::vector<Project> projects_;
    long long now_ = 0;
    stencil::net::ConnectionManager* connections_ = nullptr;
    QVector<stencil::net::ServerProject> remote_;
    QTimer* remoteTimer_ = nullptr;
    bool remoteBusy_ = false;
    QListWidget* list_ = nullptr;
    Action action_ = Action::None;
    QString selectedId_;
    QString selectedServerUrl_;
    QString newName_;
  };

}
