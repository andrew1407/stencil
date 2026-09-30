// Where a finished copy goes (browser core/project/copy/open.js): nowhere, this window or a new
// one — a local row by its id, a server copy by its link (no token rides it), an unsaved incognito
// copy by its pixels and layout.
#include "ProjectCopy.hpp"
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "IncognitoOverlay.hpp"
#include "ProjectTitleController.hpp"
#include "RemoteSession.hpp"
#include "RemoteSyncController.hpp"

namespace stencil::gui {

  namespace {
    MainWindow* freshWindow() {
      auto* win = new MainWindow(nullptr, /*restoreLast=*/false);
      win->setAttribute(Qt::WA_DeleteOnClose);
      win->show();
      return win;
    }
  }  // namespace

  void ProjectCopy::leaveCurrent() {
    if (w.incognito) return;
    if (!w.activeProjectId.isEmpty()) w.saveToActiveProject();
    else w.saveSessionNow();
  }

  void ProjectCopy::openSaved(CopyOpen open, const QString& server, const QString& id,
                              std::function<void()> then) {
    if (open == COPY_OPEN_NEW_WINDOW) {
      if (server.isEmpty()) w.parts.projects.openProjectInNewWindow(id);
      else freshWindow()->parts.projects.openServerLaunch(server, id, /*incognito=*/false);
    }
    if (open != COPY_OPEN_HERE) return then();
    leaveCurrent();
    if (!server.isEmpty()) {
      w.parts.projects.openServerProject(server, id);
      return then();
    }
    if (!w.loadProjectIntoCanvas(id, /*animate=*/true, [then](bool) { then(); })) then();
  }

  void ProjectCopy::openIncognito(const CopySource& src, support::CopyScope what, CopyOpen open) {
    QImage img = src.image;
    if (img.isNull()) img = src.bytes.isEmpty() ? QImage(src.imagePath) : QImage::fromData(src.bytes);
    const Project scoped = scopedCopy(src, what, std::string(), QString(), 0);
    const QJsonObject layout = layoutOf(src, what, img.width(), img.height());
    if (open == COPY_OPEN_NEW_WINDOW) return freshWindow()->parts.projectCopy.enterIncognito(img, layout, scoped.meta);
    leaveCurrent();
    enterIncognito(img, layout, scoped.meta);
  }

  void ProjectCopy::enterIncognito(const QImage& img, const QJsonObject& layout, const core::ProjectMeta& meta) {
    w.activeProjectId.clear();
    w.remote.session->getLink().unbind();
    w.remoteSync->stopRemotePoll();
    if (!w.incognito) {
      w.incognito = true;
      w.overlays.incognito->setActive(true);
      w.acts.incognito->blockSignals(true);
      w.acts.incognito->setChecked(true);
      w.acts.incognito->blockSignals(false);
      w.projectTitle->updateProjectTitle();
    }
    w.docSource.currentSource = QString::fromStdString(meta.source);
    w.docSource.currentResource = QString::fromStdString(meta.resource);
    w.docSource.blankColor = meta.blank ? QString::fromStdString(meta.blankColor) : QString();
    w.canvas->setBlankPage(!w.docSource.blankColor.isEmpty());
    w.loadImageWithLayout(img, layout);
    w.playImageArrival();
    w.refreshActions();
  }

}  // namespace stencil::gui
