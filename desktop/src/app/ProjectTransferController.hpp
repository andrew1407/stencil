#pragma once
#include <QByteArray>
#include <QString>
#include <functional>
#include <string>
#include <vector>
#include "fileStore.hpp"      // stencil::gui::Project + Settings + fileStore::LayoutMeta
#include "ProjectsStore.hpp"  // core::ProjectsStore

namespace stencil::net {
  class ConnectionManager;
  class ServerClient;
}

namespace stencil::gui {

  class CanvasWidget;
  class Notifications;

  // Local ↔ server transfer service, no MainWindow back-pointer: session/UI reach through Hooks.
  // Mirrors the browser's move/copyProjectToServer and
  // moveProjectToLocal/copyServerProjectToLocal.
  class ProjectTransferController {
   public:
    struct Hooks {
      std::function<stencil::net::ConnectionManager*()> connections;
      std::function<Project*(const std::string& id)> findProject;
      std::function<fileStore::LayoutMeta()> currentLayoutMeta;
      // Async HTTP fallback for extension-added projects that store only a web URL; empty bytes on
      // failure.
      std::function<void(const QString& url, std::function<void(QByteArray)> done)> fetchUrlBytes;
      std::function<QString()> activeProjectId;
      std::function<QString()> remoteAddress;
      std::function<QString()> remoteId;
      // A move relinked the open local project to a fresh server project.
      std::function<void(const QString& serverUrl, const QString& newId, const QString& name,
                         const QString& color, qint64 version)> relinkActiveToServer;
      // `animate` is true when a picture really lands, false for a rebind.
      std::function<void(const QString& id, bool animate)> loadProjectIntoCanvas;
      std::function<void()> afterChange;  // refreshActions + refreshDockMenu
    };

    ProjectTransferController(Notifications* notify, CanvasWidget* canvas, const Settings* settings,
                              core::ProjectsStore* store, std::vector<Project>* projectList,
                              Hooks hooks);

    void moveLocalProjectToServer(const QString& serverUrl, const QString& id);
    void copyLocalProjectToServer(const QString& serverUrl, const QString& id, const QString& name);
    void moveServerProjectToLocal(const QString& serverUrl, const QString& id);
    void makeLocalCopyOfServerProject(const QString& serverUrl, const QString& id, const QString& name);
    // Reports (ok, newLocalId) via `done`.
    void importServerProjectToLocal(const QString& serverUrl, const QString& id,
                                    bool removeFromServer, const QString& name,
                                    std::function<void(bool ok, QString newId)> done = {});

   private:
    // nullptr on a miss, after notifying.
    stencil::net::ServerClient* requireClient(const QString& url);
    bool localProjectOriginal(const Project& pr, QByteArray& bytes, QString& ext, int& w, int& h);
    // Reports (ok, newId, newVersion) via `done`.
    void createServerFromLocal(stencil::net::ServerClient* c, const Project& pr, const QString& name,
                               const QByteArray& bytes, const QString& ext, int w, int h,
                               std::function<void(bool ok, QString newId, qint64 newVersion)> done);

    Notifications* notify_;
    CanvasWidget* canvas_;
    const Settings* settings_;
    core::ProjectsStore* store_;
    std::vector<Project>* projectList_;
    Hooks h_;
  };

}  // namespace stencil::gui
