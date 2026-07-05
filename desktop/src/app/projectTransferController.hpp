#pragma once
#include <QByteArray>
#include <QString>
#include <functional>
#include <string>
#include <vector>
#include "fileStore.hpp"      // stencil::gui::Project + Settings + fileStore::LayoutMeta
#include "projectsStore.hpp"  // core::ProjectsStore

namespace stencil::net {
  class ConnectionManager;
  class ServerClient;
}

namespace stencil::gui {

  class CanvasWidget;
  class Notifications;

  // ── ProjectTransferController: move/copy projects local ↔ server ────────────
  // Extracted from MainWindow (the local↔server transfer subsystem). A plain (non-QObject)
  // service holding NO MainWindow back-pointer: it operates on the shared project list + store
  // and reports through Notifications, reaching the session/UI bits it can't own (the remote-link
  // relink, canvas reload, action/dock refresh, the current layout meta, findProject, the HTTP
  // fallback fetch) through the Hooks callbacks. Mirrors the browser's moveProjectToServer /
  // copyProjectToServer / moveProjectToLocal / copyServerProjectToLocal.
  class ProjectTransferController {
   public:
    struct Hooks {
      std::function<stencil::net::ConnectionManager*()> connections;
      std::function<Project*(const std::string& id)> findProject;
      std::function<fileStore::LayoutMeta()> currentLayoutMeta;
      std::function<QByteArray(const QString& url)> fetchUrlBytes;
      std::function<QString()> activeProjectId;
      std::function<QString()> remoteAddress;
      std::function<QString()> remoteId;
      // A move relinked the OPEN local project to a fresh server project: MainWindow clears the
      // active id, sets the remote-link fields, starts polling, and repaints the title.
      std::function<void(const QString& serverUrl, const QString& newId, const QString& name,
                         const QString& color, qint64 version)> relinkActiveToServer;
      std::function<void(const QString& id)> loadProjectIntoCanvas;
      std::function<void()> afterChange;  // refreshActions + refreshDockMenu
    };

    ProjectTransferController(Notifications* notify, CanvasWidget* canvas, const Settings* settings,
                              core::ProjectsStore* store, std::vector<Project>* projectList,
                              Hooks hooks);

    void moveLocalProjectToServer(const QString& serverUrl, const QString& id);
    void copyLocalProjectToServer(const QString& serverUrl, const QString& id, const QString& name);
    void moveServerProjectToLocal(const QString& serverUrl, const QString& id);
    void makeLocalCopyOfServerProject(const QString& serverUrl, const QString& id, const QString& name);
    bool importServerProjectToLocal(const QString& serverUrl, const QString& id,
                                    bool removeFromServer, const QString& name, QString* newIdOut);

   private:
    bool localProjectOriginal(const Project& pr, QByteArray& bytes, QString& ext, int& w, int& h);
    bool createServerFromLocal(stencil::net::ServerClient* c, const Project& pr, const QString& name,
                               const QByteArray& bytes, const QString& ext, int w, int h,
                               QString& newIdOut, qint64& newVersionOut);

    Notifications* notify_;
    CanvasWidget* canvas_;
    const Settings* settings_;
    core::ProjectsStore* store_;
    std::vector<Project>* projectList_;
    Hooks h_;
  };

}  // namespace stencil::gui
