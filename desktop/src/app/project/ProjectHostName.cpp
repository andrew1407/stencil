#include "MainWindow.hpp"
#include <QLabel>
#include "../../support/skinPrefs.hpp"
#include "CanvasWidget.hpp"
#include "iconInline.hpp"
#include "theme.hpp"

#include <algorithm>

// The project's name: what it reads, what a rename accepts, and the incognito tag the image bar
// shows. The window title is ProjectTitleController's.

namespace stencil::gui {

  QString ProjectHost::activeProjectName() const {
    if (activeProjectId.isEmpty()) return {};
    for (const auto& p : projectList)
      if (QString::fromStdString(p.meta.id) == activeProjectId)
        return QString::fromStdString(p.meta.name);
    return {};
  }

  QString ProjectHost::projectBaseName() const {
    const QString n = activeProjectName();
    if (!n.isEmpty()) return n;   // the project name IS the download name
    return w.canvas ? w.canvas->imageBaseName() : QStringLiteral("image");
  }

  core::ProjectsStore::NameCheck ProjectHost::checkProjectName(
      const QString& name, const QString& exceptId) const {
    std::vector<core::ProjectMeta> metas;
    for (const auto& p : projectList) metas.push_back(p.meta);
    core::ProjectsStore store;   // local; never disturbs projectsStore
    store.load(metas);
    return store.validateName(name.toStdString(), exceptId.toStdString());
  }
}  // namespace stencil::gui
