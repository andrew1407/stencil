#include "ChatPlanTarget.hpp"
#include "SharedState.hpp"

#include "MainWindow.hpp"
#include "ProjectTitleController.hpp"
#include "../../../support/modal/modalChrome.hpp"  // confirmModal — the browser-styled question
#include "DataExportController.hpp"
#include "RemoteSession.hpp"
#include "../../../canvas/CanvasWidget.hpp"
#include "../../../net/ServerClient.hpp"
#include "../../../support/displayName.hpp"
#include "../../../support/notify/Notifications.hpp"
#include "SiblingWindows.hpp"
#include <QLineEdit>

namespace stencil::gui {
  bool ChatPlanTarget::disconnectServer(const QString& server, QString* err) {
    const QStringList live = w.remote.connections ? w.remote.connections->urls() : QStringList();
    const QString url = llm::resolveServerRef(server, live);
    if (url.isEmpty()) {
      if (err)
        *err = QStringLiteral("disconnect: unknown server \"%1\" — not a live connection")
                   .arg(server);
      return false;
    }
    w.remote.connections->disconnectFrom(url);
    w.notify->info(QStringLiteral("Disconnected from %1").arg(url));
    return true;
  }

  // §10 copy: the toolbar's "Copy Image to Clipboard" path (DataExportController).
  bool ChatPlanTarget::copyImage(QString*) {
    w.dataExport->copyImageToClipboard();
    return true;
  }
  bool ChatPlanTarget::copyLayout(QString*) {
    w.dataExport->copyLayout();
    return true;
  }
  bool ChatPlanTarget::hasDrawnLines() const { return !w.canvas->getLines().empty(); }
  // Shared §10 resolution: exact name, else unique case-insensitive prefix; nullptr + *note on a miss.
  const Project* ChatPlanTarget::resolveLocalProject(const QString& name,
                                                     QString* note) const {
    std::vector<const Project*> exact, prefixed;
    const std::string want = name.toStdString();
    for (const auto& p : w.projectList) {
      if (p.meta.name == want) exact.push_back(&p);
      else if (QString::fromStdString(p.meta.name)
                   .startsWith(name, Qt::CaseInsensitive))
        prefixed.push_back(&p);
    }
    const auto& picks = exact.empty() ? prefixed : exact;
    if (picks.empty()) {
      *note = QStringLiteral("no saved project named \"%1\"").arg(name);
      return nullptr;
    }
    if (picks.size() > 1) {
      *note = QStringLiteral("\"%1\" matches %2 projects — use the full name")
                  .arg(name).arg(picks.size());
      return nullptr;
    }
    return picks.front();
  }
  // §10 removeProject: saved LOCAL projects (or the ACTIVE one for current:true), then the dialog's Delete flow. A decline is a note.
  bool ChatPlanTarget::removeProjectNamed(const QString& name, bool current,
                                          QString* note) {
    QString id, nm;
    if (current) {
      if (w.activeProjectId.isEmpty()) {
        if (!w.canvas->hasImage()) {
          *note = QStringLiteral("no saved project is open right now");
          return true;
        }
        ConfirmSpec spec;
        spec.title = "Remove image";
        spec.message = QString("Nothing is saved here — remove this editor's image and "
                               "its lines?");
        spec.confirmIcon = QStringLiteral("trash");
        spec.danger = true;
        if (!confirmModal(&w, spec)) {
          *note = QStringLiteral("removal canceled");
          return true;
        }
        w.parts.projects.resetToBlankEditor();
        w.notify->success("Editor cleared");
        return true;
      }
      id = w.activeProjectId;
      nm = support::shortName(w.activeProjectName());
    } else {
      const Project* pick = resolveLocalProject(name, note);
      if (!pick) return true;  // *note says why
      id = QString::fromStdString(pick->meta.id);
      nm = support::shortName(QString::fromStdString(pick->meta.name));
    }
    ConfirmSpec spec;
    spec.title = "Remove project";
    spec.message = QString("Remove \"%1\"? This cannot be undone.").arg(nm);
    spec.confirmIcon = QStringLiteral("trash");
    spec.danger = true;
    if (!confirmModal(&w, spec)) {
      *note = QStringLiteral("removal canceled");
      return true;
    }
    w.parts.projects.eraseLocalProject(id);
    SharedState::instance().saveProjects(&w);
    w.refreshActions();
    SiblingWindows::refreshDockMenu(w.projectList);
    w.notify->success(clearedToast(1));
    return true;
  }
  // §10 renameProject: the commitProjectName path, pre-validated so a duplicate surfaces the store's reason.
  bool ChatPlanTarget::renameActiveProject(const QString& name, QString* note) {
    const bool remote = !w.remote.session->getLink().id.isEmpty();
    if (!remote && w.activeProjectId.isEmpty()) {
      *note = QStringLiteral("no active saved project to rename");
      return true;
    }
    if (!remote) {
      const auto check = w.checkProjectName(name, w.activeProjectId);
      if (!check.ok) {
        *note = QString::fromStdString(check.reason);
        return true;
      }
    }
    w.nameBar.field->setText(name);
    w.projectTitle->commitProjectName();
    return true;
  }
  bool ChatPlanTarget::setProjectColor(const QString& color, QString* note) {
    if (w.remote.session->getLink().id.isEmpty() && w.activeProjectId.isEmpty()) {
      *note = QStringLiteral("no active project — open or save one first");
      return true;
    }
    w.parts.projects.setActiveProjectColor(color);
    return true;
  }
}  // namespace stencil::gui

