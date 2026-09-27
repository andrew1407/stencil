#include "MainWindow.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "SiblingWindows.hpp"
#include "ProjectFlows.hpp"
#include "mainWindowHelpers.hpp"
#include "Notifications.hpp"
#include "ProjectTitleController.hpp"
#include "displayName.hpp"

// The project metadata: the description and keywords dialogs, and renaming a project in the
// registry by id (the Projects window's rename and the name row's commit).

namespace stencil::gui {

  // Written back through the Projects window's store path (DescriptionDialog::apply ≙ commitRowEdit).
  void ProjectFlows::openDescription() {
    Project* pr = w.incognito ? nullptr : w.findProject(w.activeProjectId.toStdString());
    if (!pr) { w.notify->info("Save the project first to add a description"); return; }
    const QString current = QString::fromStdString(pr->meta.description);
    DescriptionDialog dlg(current, &w);
    if (w.execMaybePopover(dlg, w.acts.description) != QDialog::Accepted) return;
    const QString text = dlg.text();
    if (text == current) return;
    if (DescriptionDialog::apply(w.projectList, w.activeProjectId, text, nowMs()))
      w.notify->success(text.isEmpty() ? "Description cleared" : "Description saved");
  }

  // Same way (KeywordsDialog::apply ≙ commitRowEdit).
  void ProjectFlows::openKeywords() {
    Project* pr = w.incognito ? nullptr : w.findProject(w.activeProjectId.toStdString());
    if (!pr) { w.notify->info("Save the project first to add keywords"); return; }
    QStringList current;
    for (const auto& k : pr->meta.keywords) current << QString::fromStdString(k);
    KeywordsDialog dlg(current, &w);
    if (w.execMaybePopover(dlg, w.acts.keywords) != QDialog::Accepted) return;
    const QStringList next = dlg.keywords();
    if (next == current) return;
    if (KeywordsDialog::apply(w.projectList, w.activeProjectId, next, nowMs()))
      w.notify->success(next.isEmpty() ? "Keywords cleared" : "Keywords saved");
  }

  bool ProjectFlows::renameProjectById(const QString& id, const QString& rawName) {
    const QString name = rawName.trimmed();
    Project* pr = w.findProject(id.toStdString());
    if (!pr) return false;
    const auto check = w.checkProjectName(name, id);
    if (!check.ok) {
      w.notify->error(QString::fromStdString(check.reason));
      return false;
    }
    pr->meta.name = name.toStdString();
    // Downloads use projectBaseName(), so there is no separate image name.
    fileStore::saveProjects(w.projectList);
    SiblingWindows::refreshDockMenu(w.projectList);
    if (w.activeProjectId == id) w.projectTitle->updateProjectTitle();
    w.notify->success(QString("Renamed to \"%1\"").arg(support::shortName(name)));
    return true;
  }

}  // namespace stencil::gui
