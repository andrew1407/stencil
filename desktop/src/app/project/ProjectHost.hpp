#pragma once
// The project a window holds, and the flows that change it: which project is open, the list every
// window shares, where the document came from, incognito, and the store that names and sweeps
// projects; ProjectFlows and ProjectCopy reach the rest of the window only through ProjectWindow.
// Browser twin: DrawingApp.activeProjectId + storage.incognito over core/project/store/projectsStore.js.
#include "DocumentSource.hpp"
#include "ProjectCopy.hpp"
#include "ProjectFlows.hpp"
#include "ProjectWindow.hpp"
#include "fileStore.hpp"

#include <QString>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace stencil::gui {

  class ProjectHost {
   public:
    ProjectHost(std::vector<Project>& list, ProjectWindow seam)
        : projectList(list), w(std::move(seam)), flows(*this, w), copy(*this, w) {}
    ProjectHost(const ProjectHost&) = delete;
    ProjectHost& operator=(const ProjectHost&) = delete;

    // Browser switchToProject(); animate=false for a REBIND, where a dust arrival would be a lie.
    // False without such a project; `landed` hears whether its off-thread decode went in.
    bool loadProjectIntoCanvas(const QString& id, bool animate = true, std::function<void(bool)> landed = {});
    bool projectOpenInOtherWindow(const QString& id) const;
    void createLocalProject(const QString& name, bool announce = true, bool fromFile = false);
    void adoptCanvasAsLocalProject();
    void saveToActiveProject();
    QString promoteIncognitoToLocal(const QString& name = QString());
    // Browser twin: validateName/nameExists.
    QString activeProjectName() const;
    QString projectBaseName() const;
    core::ProjectsStore::NameCheck checkProjectName(const QString& name, const QString& exceptId) const;

    // The row `id` names in the shared list, or null.
    Project* findProject(const std::string& id) {
      for (auto& p : projectList)
        if (p.meta.id == id) return &p;
      return nullptr;
    }

    core::ProjectsStore projectsStore;
    std::vector<Project>& projectList;   // SharedState's, shared by every window
    QString activeProjectId;
    DocumentSource docSource;
    bool incognito = false;
    bool filterDirty = false;

   private:
    ProjectWindow w;

   public:
    ProjectFlows flows;
    ProjectCopy copy;
  };

}  // namespace stencil::gui
