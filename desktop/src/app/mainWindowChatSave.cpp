#include "mainWindow.hpp"
#include "mainWindowChatParts.hpp"
#include "mainWindowHelpers.hpp"
#include "chatMenuPanel.hpp"
#include "chatPlanTarget.hpp"
#include "canvasWidget.hpp"
#include "chatDock.hpp"
#include "displayName.hpp"
#include "guiHelpers.hpp"   // confirmYesNo()
#include "iconSet.hpp"
#include "mediaLoader.hpp"
#include "notifications.hpp"
#include "opPlan.hpp"
#include "opRegistry.hpp"   // promptText() — the §4 canon
#include "planExecutor.hpp"
#include "qtLlmTransport.hpp"
#include "remoteSession.hpp"
#include "serverClient.hpp"
#include "connectionStore.hpp"
#include "theme.hpp"
#include "tipContent.hpp"   // currentPalette() — the colours rich tooltips are drawn in
#include "../support/localPath.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QWidgetAction>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <algorithm>

namespace stencil::gui {

  // §2.1 `save`

  QString MainWindow::uniqueLocalProjectName(const QString& wanted) const {
    std::vector<core::ProjectMeta> metas;
    for (const auto& p : projectList_) metas.push_back(p.meta);
    core::ProjectsStore store;   // local; never disturbs projectsStore_
    store.load(metas);
    if (!store.nameExists(wanted.toStdString())) return wanted;
    for (int n = 2; n < 1000; ++n) {
      const QString candidate = QStringLiteral("%1 %2").arg(wanted).arg(n);
      if (!store.nameExists(candidate.toStdString())) return candidate;
    }
    return wanted;
  }

  QString MainWindow::chatSaveBaseName(const QString& requested) const {
    QString name = requested.trimmed();
    // The attachment the plan is working on names it — a 3-image plan then
    // leaves 3 distinctly named projects.
    if (name.isEmpty() && chatActiveAttachment_ >= 1)
      name = QFileInfo(chatTurnAttachmentNames_.value(chatActiveAttachment_ - 1))
                 .completeBaseName()
                 .trimmed();
    if (name.isEmpty()) name = activeProjectName();
    if (name.isEmpty() && canvas_ && canvas_->hasImage()) name = canvas_->imageBaseName();
    if (name.isEmpty()) name = QStringLiteral("Untitled");
    return name.left(core::ProjectsStore::kMaxNameLength);  // core validateName's cap
  }

  bool MainWindow::chatSaveProject(const QString& name, const QString& dest, QString* err) {
    if (!canvas_->hasImage()) {   // the executor notes this case before calling
      if (err) *err = QStringLiteral("save: there is no image to save");
      return false;
    }
    // Incognito blocks what the app would write BY ITSELF, not what the user asks for: writing
    // a file to a path they named is the Save Image… they can already do from the toolbar, and
    // a pathless save promotes the session out of incognito (below) instead of refusing.
    // §10: a destination the user named — a .stencil bundle, an image file, or a folder
    // (which gets "<name>.png", the format Save-as offers). Anything else stays the
    // editor's own project store, exactly as before.
    if (!dest.isEmpty()) {
      const QString base = chatSaveBaseName(name);
      QString path = support::expandHomePath(dest);
      const bool isFile = !support::fileExtensionOf(path).isEmpty();
      if (!isFile) {
        QDir().mkpath(path);
        if (path.endsWith(QLatin1Char('/'))) path.chop(1);
        path += QStringLiteral("/") + base + QStringLiteral(".png");
      }
      if (path.endsWith(QStringLiteral(".stencil"), Qt::CaseInsensitive)) {
        if (!writeFileBytes(path, buildStencilBytes())) {
          if (err) *err = QStringLiteral("save: could not write %1").arg(path);
          return false;
        }
      } else if (!canvas_->renderToImage(true).save(path)) {
        if (err) *err = QStringLiteral("save: could not write %1").arg(path);
        return false;
      }
      notify_->success(QStringLiteral("Saved %1").arg(support::shortName(path)));
      return true;
    }
    if (incognito_) {   // leaving incognito IS the save the user asked for
      const QString promoted = promoteIncognitoToLocal(chatSaveBaseName(name));
      notify_->success(QStringLiteral("Left incognito — saved \"%1\"")
                           .arg(support::shortName(promoted)));
      return true;
    }
    // A FRESH project per save (the create-project path, minus its dialogs), so
    // each image of a multi-image plan lands as its own project.
    const QString unique = uniqueLocalProjectName(chatSaveBaseName(name));
    createLocalProject(unique, /*announce=*/false);
    notify_->success(QStringLiteral("Saved \"%1\"").arg(support::shortName(unique)));
    return true;
  }

  // §10 openFile: a user-named local file, dispatched by extension the way /upload does in
  // the console — a project restores everything, a layout draws onto the current picture,
  // and a picture/video goes through the same awaited source load openUrl uses.
  bool MainWindow::chatOpenFile(const QString& raw, QString* err) {
    const QString path = support::expandHomePath(raw);
    if (!QFileInfo::exists(path)) {
      if (err) *err = QStringLiteral("openFile: %1 does not exist").arg(path);
      return false;
    }
    if (path.endsWith(QStringLiteral(".stencil"), Qt::CaseInsensitive)) {
      openProjectFile(path);
      return true;
    }
    if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
      if (!canvas_->hasImage()) {
        if (err) *err = QStringLiteral("openFile: load a picture before applying a layout");
        return false;
      }
      applyLayoutFromSource(path);
      return true;
    }
    notify_->info(QStringLiteral("Opening %1").arg(support::shortName(path)));
    QString why;
    if (chatLoadSource(path, incognito_, &why)) return true;
    if (err) *err = QStringLiteral("openFile: %1").arg(why);
    return false;
  }

  // Open `src` (a URL or a local path) here and BLOCK until MediaLoader resolves — the plan
  // executor awaits its loads so the next action edits the new picture, not the old one.
  bool MainWindow::chatLoadSource(const QString& src, bool incognito, QString* why) {
    constexpr int kSourceWaitMs = 20000;  // finite: a stalled load can't hang the plan
    ensureMediaLoader();  // before OUR connects, so the canvas adopts first
    QEventLoop loop;
    bool loaded = false, failed = false;
    const auto cLoaded = QObject::connect(mediaLoader_, &MediaLoader::loaded, &loop,
                                          [&](const QImage&, const QString&) {
                                            loaded = true;
                                            loop.quit();
                                          });
    const auto cFailed = QObject::connect(mediaLoader_, &MediaLoader::failed, &loop,
                                          [&](const QString& msg) {
                                            failed = true;
                                            if (why) *why = msg;
                                            loop.quit();
                                          });
    QTimer::singleShot(kSourceWaitMs, &loop, [&loop] { loop.quit(); });
    openSourceHere(src, 0, incognito);
    if (!loaded && !failed) loop.exec();  // guards a synchronous outcome
    QObject::disconnect(cLoaded);
    QObject::disconnect(cFailed);
    if (failed) return false;
    if (!loaded) {
      if (why) *why = QStringLiteral("timed out loading %1").arg(src);
      return false;
    }
    return true;
  }
}  // namespace stencil::gui

