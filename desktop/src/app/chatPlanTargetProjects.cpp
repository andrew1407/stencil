#include "chatPlanTarget.hpp"

#include "mainWindow.hpp"
#include "../support/modalChrome.hpp"  // confirmModal — the browser-styled question
#include "mainWindowHelpers.hpp"
#include "dataExportController.hpp"
#include "remoteSession.hpp"
#include "../canvas/canvasWidget.hpp"
#include "../net/connectionStore.hpp"
#include "../net/serverClient.hpp"
#include "../support/displayName.hpp"
#include "../support/notifications.hpp"
#include "../support/theme.hpp"

#include <QAction>
#include <QApplication>
#include <QJsonObject>
#include <QTimer>

namespace stencil::gui {
  // §10 openProject: removeProject's resolution, then the projects dialog's open path —
  // including its unsaved-replace confirm when no saved project backs the editor's work.
  bool ChatPlanTarget::openProjectNamed(const QString& name, bool last, QString* note) {
    // "the last project I worked on": the most recently edited one, resolved HERE off the
    // store's updatedAt — the model never sees the project list (chatSession.js parity).
    const Project* pick = nullptr;
    if (last) {
      for (const auto& p : w_.projectList_)
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
    const bool unsaved = w_.canvas_->hasImage() && w_.activeProjectId_.isEmpty() &&
                         w_.remoteSession_->link().id.isEmpty();
    ConfirmSpec openSpec;
    openSpec.title = "Open project";
    openSpec.message = QString("Open \"%1\"? Any unsaved changes in the current window will be "
                               "replaced.")
                           .arg(nm);
    openSpec.confirmLabel = "Open";
    openSpec.confirmIcon = QStringLiteral("folder");
    if (unsaved && !confirmModal(&w_, openSpec)) {
      *note = QStringLiteral("open canceled");
      return true;
    }
    if (!w_.loadProjectIntoCanvas(id)) {
      *note = QStringLiteral("could not open \"%1\"").arg(nm);
      return true;
    }
    return true;
  }
  // §10 incognito: the actIncognito_ toggle — only togglable on a blank
  // (imageless) editor, exactly like the action's own enabled gate.
  bool ChatPlanTarget::setIncognito(bool on, QString* note) {
    if (!on && w_.incognito_ && w_.canvas_->hasImage()) {
      // "make this not incognito" with work on screen: keep the work — leave
      // incognito and save it as a local project (publishIncognitoToServer's move).
      const QString promoted = w_.promoteIncognitoToLocal();
      *note = QStringLiteral("left incognito — saved as the local project \"%1\"")
                  .arg(support::shortName(promoted));
      return true;
    }
    if (w_.canvas_->hasImage()) {
      *note = QStringLiteral("incognito can only be turned ON from a blank editor");
      return true;
    }
    if (w_.actIncognito_ && w_.actIncognito_->isChecked() != on)
      w_.actIncognito_->setChecked(on);  // its toggled handler applies + notifies
    return true;
  }
  // §10 chatPanel: the panel's OWN placement, through the very calls its title-bar buttons
  // make, so a spoken "put the chat on the right" lands where a click would have. A dock
  // with no "open" shows the panel too (browser chatSession.js setChatPlacement parity).
  bool ChatPlanTarget::setChatPlacement(int open, const QString& dock, QString* note) {
    if (!w_.chatDock_ || !w_.actChat_) {
      *note = QStringLiteral("there is no assistant panel here");
      return true;
    }
    // Show FIRST, then place: the placement paths animate the panel on screen (and
    // toggleChatFloat only has a window to lift once it is showing), so a hidden dock
    // asked to float would have stayed docked. Closing comes last, for the same reason.
    const bool show = open < 0 ? !dock.isEmpty() : open == 1;
    if (show && !w_.actChat_->isChecked()) w_.actChat_->setChecked(true);
    if (!dock.isEmpty()) {
      if (dock == QLatin1String("float")) {
        if (!w_.chatDock_->isFloating()) w_.toggleChatFloat();
      } else {
        const Qt::DockWidgetArea area = dock == QLatin1String("left")    ? Qt::LeftDockWidgetArea
                                        : dock == QLatin1String("right")  ? Qt::RightDockWidgetArea
                                        : dock == QLatin1String("top")    ? Qt::TopDockWidgetArea
                                                                          : Qt::BottomDockWidgetArea;
        w_.dockChatTo(area);
      }
    }
    // The toggle's own handler runs the open/close flight, so this IS the click.
    if (!show && w_.actChat_->isChecked()) w_.actChat_->setChecked(false);
    return true;
  }

  // §10 dialog: the editor's own windows, through the very QActions their toolbar buttons
  // drive. Opened on the NEXT event-loop turn, never inline: they exec() modally, which
  // would park the whole plan behind a window the user has not been told about yet.
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
    QAction* act = name == QLatin1String("projects")    ? w_.actProjects_
                   : name == QLatin1String("servers")   ? w_.actConnect_
                   : name == QLatin1String("shortcuts") ? w_.actShortcuts_
                   : name == QLatin1String("visuals")   ? w_.actSettings_
                                                        : w_.actInfo_;
    if (!act || !act->isEnabled()) {
      *note = QStringLiteral("the %1 window is not available right now").arg(name);
      return true;
    }
    QTimer::singleShot(0, &w_, [act] { act->trigger(); });
    return true;
  }

  // §10 clearProjects: every LOCAL project through the same machinery as the
  // dialog's "Clear All (Local)" — server projects are never touched from chat.
  bool ChatPlanTarget::clearProjects(bool keepCurrent, QString* note) {
    // `keepCurrent` = "delete the others": the open project stays where it is. Without it
    // the model clears the lot, then loses the project outright when no working image is
    // left to re-save.
    const QString keepId = keepCurrent ? w_.activeProjectId_ : QString();
    const bool keeping = !keepId.isEmpty();
    const int total = static_cast<int>(w_.projectList_.size());
    QString keptName;
    int n = 0;
    for (const Project& p : w_.projectList_) {
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
    if (!confirmModal(&w_, spec)) {
      *note = QStringLiteral("clear canceled");
      return true;
    }
    const bool hadActive = !w_.activeProjectId_.isEmpty();
    if (keeping) {
      std::vector<Project> kept;
      for (const Project& p : w_.projectList_)
        if (QString::fromStdString(p.meta.id) == keepId) kept.push_back(p);
      w_.projectList_ = std::move(kept);
    } else {
      w_.projectList_.clear();
      if (hadActive) w_.resetToBlankEditor();   // the open one went with them
    }
    fileStore::saveProjects(w_.projectList_);
    w_.refreshActions();
    w_.refreshDockMenu();
    w_.notify_->success(QString("Cleared %1 local project(s)").arg(n));
    return true;
  }
  // §10 clearChat: only FLAG the request — the confirm + clear run once the
  // whole turn settles (chatTurnSettled); a modal here would stall the plan.
  bool ChatPlanTarget::clearChat(QString*) {
    w_.chatClearPending_ = true;
    return true;
  }
  // `image`: the turn's Nth attachment becomes the working image, through the
  // same bare-QImage adoption the empty-canvas case uses. An index this turn
  // cannot satisfy is reported back (the executor's skipped-action note).
  bool ChatPlanTarget::loadAttachment(int index, QString* err) {
    if (index < 1 || index > w_.chatTurnAttachments_.size()) {
      if (err)
        *err = QStringLiteral("this message attached %1 image(s)")
                   .arg(w_.chatTurnAttachments_.size());
      return false;
    }
    w_.loadImageWithLayout(w_.chatTurnAttachments_.at(index - 1), QJsonObject());
    w_.chatActiveAttachment_ = index;   // it names an unnamed `save`
    w_.refreshActions();
    w_.onSelectionChanged();
    w_.updateImageSizeInfo();
    return true;
  }
  // `save`: a LOCAL project, never a server publish (that stays a user action).
  // `dest` (already echo-checked) writes to that folder/file instead.
  bool ChatPlanTarget::saveProject(const QString& name, const QString& dest,
                                   QString* err) {
    return w_.chatSaveProject(name, dest, err);
  }

  // The pool the executor's echo-guard checks: the user's OWN turns.
  QString ChatPlanTarget::userTypedText() const {
    QStringList parts;
    for (const auto& m : w_.chatHistory_)
      if (m.role == QLatin1String("user")) parts << m.text;
    return parts.join(QLatin1Char('\n'));
  }

  QImage ChatPlanTarget::renderResult() const { return w_.canvas_->renderToImage(true); }
}  // namespace stencil::gui

