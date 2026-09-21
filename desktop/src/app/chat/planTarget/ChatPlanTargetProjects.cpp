#include "ChatPlanTarget.hpp"

#include "MainWindow.hpp"
#include "../../../support/modal/modalChrome.hpp"  // confirmModal — the browser-styled question
#include "mainWindowHelpers.hpp"
#include "DataExportController.hpp"
#include "RemoteSession.hpp"
#include "../../../canvas/CanvasWidget.hpp"
#include "../../../net/connectionStore.hpp"
#include "../../../net/ServerClient.hpp"
#include "../../../support/displayName.hpp"
#include "../../../support/notify/Notifications.hpp"
#include "../../../support/theme/theme.hpp"

#include <QAction>
#include <QApplication>
#include <QJsonObject>
#include <QTimer>

namespace stencil::gui {
  // §10 openProject: the projects dialog's open path, unsaved-replace confirm included.
  bool ChatPlanTarget::openProjectNamed(const QString& name, bool last, QString* note) {
    // "the last project I worked on" resolves HERE off updatedAt — the model never sees the list (session.js parity).
    const Project* pick = nullptr;
    if (last) {
      for (const auto& p : w.projectList)
        if (!pick || p.meta.updatedAt > pick->meta.updatedAt) pick = &p;
      if (!pick) {
        *note = QStringLiteral("there are no saved projects yet");
        return true;
      }
    } else {
      pick = resolveLocalProject(name, note);
    }
    if (!pick) return true;  // *note says why
    const QString id = QString::fromStdString(pick->meta.id);
    const QString nm = support::shortName(QString::fromStdString(pick->meta.name));
    const bool unsaved = w.canvas->hasImage() && w.activeProjectId.isEmpty() &&
                         w.remoteSession->getLink().id.isEmpty();
    ConfirmSpec openSpec;
    openSpec.title = "Open project";
    openSpec.message = QString("Open \"%1\"? Any unsaved changes in the current window will be "
                               "replaced.")
                           .arg(nm);
    openSpec.confirmLabel = "Open";
    openSpec.confirmIcon = QStringLiteral("folder");
    if (unsaved && !confirmModal(&w, openSpec)) {
      *note = QStringLiteral("open canceled");
      return true;
    }
    if (!w.loadProjectIntoCanvas(id)) {
      *note = QStringLiteral("could not open \"%1\"").arg(nm);
      return true;
    }
    return true;
  }
  // §10 incognito: only togglable on a blank editor, like the action's own gate.
  bool ChatPlanTarget::setIncognito(bool on, QString* note) {
    if (!on && w.incognito && w.canvas->hasImage()) {
      // Leaving incognito with work on screen keeps it as a local project.
      const QString promoted = w.promoteIncognitoToLocal();
      *note = QStringLiteral("left incognito — saved as the local project \"%1\"")
                  .arg(support::shortName(promoted));
      return true;
    }
    if (w.canvas->hasImage()) {
      *note = QStringLiteral("incognito can only be turned ON from a blank editor");
      return true;
    }
    if (w.actIncognito && w.actIncognito->isChecked() != on)
      w.actIncognito->setChecked(on);  // its toggled handler applies + notifies
    return true;
  }
  // §10 chatPanel: the panel's OWN placement calls (browser session.js setChatPlacement parity).
  bool ChatPlanTarget::setChatPlacement(int open, const QString& dock, QString* note) {
    if (!w.chatDock || !w.actChat) {
      *note = QStringLiteral("there is no assistant panel here");
      return true;
    }
    // Show FIRST, then place: the placement paths animate a shown panel, and a hidden dock asked to float would stay docked.
    const bool show = open < 0 ? !dock.isEmpty() : open == 1;
    if (show && !w.actChat->isChecked()) w.actChat->setChecked(true);
    if (!dock.isEmpty()) {
      if (dock == QLatin1String("float")) {
        if (!w.chatDock->isFloating()) w.toggleChatFloat();
      } else {
        const Qt::DockWidgetArea area = dock == QLatin1String("left")    ? Qt::LeftDockWidgetArea
                                        : dock == QLatin1String("right")  ? Qt::RightDockWidgetArea
                                        : dock == QLatin1String("top")    ? Qt::TopDockWidgetArea
                                                                          : Qt::BottomDockWidgetArea;
        w.dockChatTo(area);
      }
    }
    if (!show && w.actChat->isChecked()) w.actChat->setChecked(false);
    return true;
  }

  // §10 dialog: opened on the NEXT event-loop turn — they exec() modally, which would park the plan behind an unannounced window.
  bool ChatPlanTarget::openDialog(const QString& name, QString* note) {
    if (name.isEmpty()) {
      QWidget* open = QApplication::activeModalWidget();
      if (!open) {
        *note = QStringLiteral("no window is open");
        return true;
      }
      QTimer::singleShot(0, open, [open] { open->close(); });
      return true;
    }
    QAction* act = name == QLatin1String("projects")    ? w.actProjects
                   : name == QLatin1String("servers")   ? w.actConnect
                   : name == QLatin1String("shortcuts") ? w.actShortcuts
                   : name == QLatin1String("visuals")   ? w.actSettings
                                                        : w.actInfo;
    if (!act || !act->isEnabled()) {
      *note = QStringLiteral("the %1 window is not available right now").arg(name);
      return true;
    }
    QTimer::singleShot(0, &w, [act] { act->trigger(); });
    return true;
  }

  // §10 clearProjects: LOCAL projects only, through the dialog's "Clear All (Local)" machinery.
  bool ChatPlanTarget::clearProjects(bool keepCurrent, QString* note) {
    // `keepCurrent` = "delete the others": without it the model clears the lot and loses the project outright.
    const QString keepId = keepCurrent ? w.activeProjectId : QString();
    const bool keeping = !keepId.isEmpty();
    const int total = static_cast<int>(w.projectList.size());
    QString keptName;
    int n = 0;
    for (const Project& p : w.projectList) {
      if (keeping && QString::fromStdString(p.meta.id) == keepId) {
        keptName = support::shortName(QString::fromStdString(p.meta.name));
        continue;
      }
      ++n;
    }
    if (n == 0) {
      *note = total ? QStringLiteral("no other saved projects to clear")
                    : QStringLiteral("no saved projects to clear");
      return true;
    }
    ConfirmSpec spec;
    spec.title = keeping ? "Clear other projects" : "Clear all projects";
    spec.message = keeping
        ? QString("Are you sure? This removes the %1 other local project(s), keeping "
                  "\"%2\", and cannot be undone. Server projects are not affected.")
              .arg(n)
              .arg(keptName)
        : QString("Are you sure? This removes all %1 local project(s) and cannot be undone. "
                  "Server projects are not affected.")
              .arg(n);
    spec.confirmIcon = QStringLiteral("trash");
    spec.danger = true;
    if (!confirmModal(&w, spec)) {
      *note = QStringLiteral("clear canceled");
      return true;
    }
    const bool hadActive = !w.activeProjectId.isEmpty();
    if (keeping) {
      std::vector<Project> kept;
      for (const Project& p : w.projectList)
        if (QString::fromStdString(p.meta.id) == keepId) kept.push_back(p);
      w.projectList = std::move(kept);
    } else {
      w.projectList.clear();
      if (hadActive) w.resetToBlankEditor();   // the open one went with them
    }
    fileStore::saveProjects(w.projectList);
    w.refreshActions();
    w.refreshDockMenu();
    w.notify->success(QString("Cleared %1 local project(s)").arg(n));
    return true;
  }
  // §10 clearChat: only FLAG it — the confirm runs once the turn settles; a modal here would stall the plan.
  bool ChatPlanTarget::clearChat(QString*) {
    w.chatClearPending = true;
    return true;
  }
  // `image`: the turn's Nth attachment becomes the working image; an unsatisfiable index is reported back.
  bool ChatPlanTarget::loadAttachment(int index, QString* err) {
    if (index < 1 || index > w.chatTurnAttachments.size()) {
      if (err)
        *err = QStringLiteral("this message attached %1 image(s)")
                   .arg(w.chatTurnAttachments.size());
      return false;
    }
    w.loadImageWithLayout(w.chatTurnAttachments.at(index - 1), QJsonObject());
    w.chatActiveAttachment = index;   // it names an unnamed `save`
    w.refreshActions();
    w.onSelectionChanged();
    w.updateImageSizeInfo();
    return true;
  }
  // `save`: a LOCAL project, never a server publish; `dest` (echo-checked) redirects it.
  bool ChatPlanTarget::saveProject(const QString& name, const QString& dest,
                                   QString* err) {
    return w.chatSaveProject(name, dest, err);
  }

  QString ChatPlanTarget::userTypedText() const {
    QStringList parts;
    for (const auto& m : w.chatHistory)
      if (m.role == QLatin1String("user")) parts << m.text;
    return parts.join(QLatin1Char('\n'));
  }

  QImage ChatPlanTarget::renderResult() const { return w.canvas->renderToImage(true); }
}  // namespace stencil::gui

