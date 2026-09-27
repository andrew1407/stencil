#include "MainWindow.hpp"
#include "ChatAppliers.hpp"
#include "ChatSessionController.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "displayName.hpp"
#include "Notifications.hpp"
#include "PlanAwait.hpp"
#include "../../../support/localPath.hpp"
#include "../../../support/rowWork.hpp"

// The assistant's `save` op (llm-contract.md §2.1): the name the project is filed under, and the
// write — to a path the user named (rendered and written on the pool), or into the local project list.

namespace stencil::gui {

  QString ChatAppliers::uniqueLocalProjectName(const QString& wanted) const {
    std::vector<core::ProjectMeta> metas;
    for (const auto& p : w.projectList) metas.push_back(p.meta);
    core::ProjectsStore store;   // local; never disturbs projectsStore
    store.load(metas);
    if (!store.nameExists(wanted.toStdString())) return wanted;
    for (int n = 2; n < 1000; ++n) {
      const QString candidate = QStringLiteral("%1 %2").arg(wanted).arg(n);
      if (!store.nameExists(candidate.toStdString())) return candidate;
    }
    return wanted;
  }

  QString ChatAppliers::chatSaveBaseName(const QString& requested) const {
    QString name = requested.trimmed();
    // The attachment the plan works on names it — a 3-image plan leaves 3 named projects.
    const int active = w.chatSession->chatActiveAttachment;
    if (name.isEmpty() && active >= 1)
      name = QFileInfo(w.chatSession->chatTurnAttachmentNames.value(active - 1))
                 .completeBaseName()
                 .trimmed();
    if (name.isEmpty()) name = w.activeProjectName();
    if (name.isEmpty() && w.canvas && w.canvas->hasImage()) name = w.canvas->imageBaseName();
    if (name.isEmpty()) name = QStringLiteral("Untitled");
    return name.left(core::ProjectsStore::MAX_NAME_LENGTH);  // core validateName's cap
  }

  // A folder takes "<name>.png"; a path with an extension is the file itself.
  QString ChatAppliers::chatSavePath(const QString& name, const QString& dest) const {
    QString path = support::expandHomePath(dest);
    if (!support::fileExtensionOf(path).isEmpty()) return path;
    QDir().mkpath(path);
    if (path.endsWith(QLatin1Char('/'))) path.chop(1);
    return path + QStringLiteral("/") + chatSaveBaseName(name) + QStringLiteral(".png");
  }

  // A named destination renders and writes off the GUI thread; a close lapses it as saved.
  void ChatAppliers::chatSaveProjectThen(const QString& name, const QString& dest, llm::OpDone done) {
    if (dest.isEmpty() || !w.canvas->hasImage()) {
      QString err;
      const bool ok = chatSaveProject(name, dest, &err);
      return done(ok, err);
    }
    const QString path = chatSavePath(name, dest);
    const bool project = path.endsWith(QStringLiteral(".stencil"), Qt::CaseInsensitive);
    const QByteArray bytes = project ? w.buildStencilBytes() : QByteArray();
    const std::shared_ptr<CanvasScene> scene = project ? nullptr : w.canvas->renderCopy();
    PlanAwait::start(
        w, w.pop, std::move(done),
        [&](PlanAwait* await) {
          support::runOnPool<bool>(
              &w,
              [path, bytes, scene] {
                return scene ? scene->renderToImage(true).save(path) : writeFileBytes(path, bytes);
              },
              [this, path, guard = QPointer<PlanAwait>(await)](bool ok) {
                if (!guard || guard->isSettled()) return;
                if (ok) w.notify->success(QStringLiteral("Saved %1").arg(support::shortName(path)));
                guard->settle(ok, ok ? QString() : QStringLiteral("save: could not write %1").arg(path));
              });
        },
        0, /*lapseOk=*/true);
  }

  bool ChatAppliers::chatSaveProject(const QString& name, const QString& dest, QString* err) {
    if (!w.canvas->hasImage()) {   // the executor notes this case before calling
      if (err) *err = QStringLiteral("save: there is no image to save");
      return false;
    }
    // Incognito blocks what the app writes BY ITSELF, not a path the user named (§10: a .stencil, an image file, or a folder → "<name>.png");
    // a pathless save promotes the session out of incognito.
    if (!dest.isEmpty()) {
      const QString path = chatSavePath(name, dest);
      if (path.endsWith(QStringLiteral(".stencil"), Qt::CaseInsensitive)) {
        if (!writeFileBytes(path, w.buildStencilBytes())) {
          if (err) *err = QStringLiteral("save: could not write %1").arg(path);
          return false;
        }
      } else if (!w.canvas->renderToImage(true).save(path)) {
        if (err) *err = QStringLiteral("save: could not write %1").arg(path);
        return false;
      }
      w.notify->success(QStringLiteral("Saved %1").arg(support::shortName(path)));
      return true;
    }
    if (w.incognito) {   // leaving incognito IS the save the user asked for
      const QString promoted = w.promoteIncognitoToLocal(chatSaveBaseName(name));
      w.notify->success(QStringLiteral("Left incognito — saved \"%1\"")
                           .arg(support::shortName(promoted)));
      return true;
    }
    // A FRESH project per save, so each image of a multi-image plan lands as its own.
    const QString unique = uniqueLocalProjectName(chatSaveBaseName(name));
    w.createLocalProject(unique, /*announce=*/false);
    w.notify->success(QStringLiteral("Saved \"%1\"").arg(support::shortName(unique)));
    return true;
  }

}  // namespace stencil::gui

