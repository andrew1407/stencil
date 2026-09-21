#include "MainWindow.hpp"
#include "mainWindowChatParts.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatMenuPanel.hpp"
#include "ChatPlanTarget.hpp"
#include "CanvasWidget.hpp"
#include "ChatDock.hpp"
#include "displayName.hpp"
#include "guiHelpers.hpp"   // confirmYesNo()
#include "iconSet.hpp"
#include "MediaLoader.hpp"
#include "Notifications.hpp"
#include "opPlan.hpp"
#include "opRegistry.hpp"   // promptText() — the §4 canon
#include "planExecutor.hpp"
#include "QtLlmTransport.hpp"
#include "RemoteSession.hpp"
#include "ServerClient.hpp"
#include "connectionStore.hpp"
#include "theme.hpp"
#include "tipContent.hpp"   // currentPalette() — the colours rich tooltips are drawn in
#include "../../support/localPath.hpp"

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
    for (const auto& p : projectList) metas.push_back(p.meta);
    core::ProjectsStore store;   // local; never disturbs projectsStore
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
    // The attachment the plan works on names it — a 3-image plan leaves 3 named projects.
    if (name.isEmpty() && chatActiveAttachment >= 1)
      name = QFileInfo(chatTurnAttachmentNames.value(chatActiveAttachment - 1))
                 .completeBaseName()
                 .trimmed();
    if (name.isEmpty()) name = activeProjectName();
    if (name.isEmpty() && canvas && canvas->hasImage()) name = canvas->imageBaseName();
    if (name.isEmpty()) name = QStringLiteral("Untitled");
    return name.left(core::ProjectsStore::MAX_NAME_LENGTH);  // core validateName's cap
  }

  bool MainWindow::chatSaveProject(const QString& name, const QString& dest, QString* err) {
    if (!canvas->hasImage()) {   // the executor notes this case before calling
      if (err) *err = QStringLiteral("save: there is no image to save");
      return false;
    }
    // Incognito blocks what the app writes BY ITSELF, not a path the user named (§10: a .stencil, an image file, or a folder → "<name>.png");
    // a pathless save promotes the session out of incognito.
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
      } else if (!canvas->renderToImage(true).save(path)) {
        if (err) *err = QStringLiteral("save: could not write %1").arg(path);
        return false;
      }
      notify->success(QStringLiteral("Saved %1").arg(support::shortName(path)));
      return true;
    }
    if (incognito) {   // leaving incognito IS the save the user asked for
      const QString promoted = promoteIncognitoToLocal(chatSaveBaseName(name));
      notify->success(QStringLiteral("Left incognito — saved \"%1\"")
                           .arg(support::shortName(promoted)));
      return true;
    }
    // A FRESH project per save, so each image of a multi-image plan lands as its own.
    const QString unique = uniqueLocalProjectName(chatSaveBaseName(name));
    createLocalProject(unique, /*announce=*/false);
    notify->success(QStringLiteral("Saved \"%1\"").arg(support::shortName(unique)));
    return true;
  }

  // §10 openFile: dispatched by extension as /upload does in the console.
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
      if (!canvas->hasImage()) {
        if (err) *err = QStringLiteral("openFile: load a picture before applying a layout");
        return false;
      }
      applyLayoutFromSource(path);
      return true;
    }
    notify->info(QStringLiteral("Opening %1").arg(support::shortName(path)));
    QString why;
    if (chatLoadSource(path, incognito, &why)) return true;
    if (err) *err = QStringLiteral("openFile: %1").arg(why);
    return false;
  }

  // BLOCK until MediaLoader resolves — the executor awaits loads so the next action edits the new picture.
  bool MainWindow::chatLoadSource(const QString& src, bool incognito, QString* why, int frame) {
    constexpr int SOURCE_WAIT_MS = 20000;  // finite: a stalled load can't hang the plan
    ensureMediaLoader();  // before OUR connects, so the canvas adopts first
    QEventLoop loop;
    bool loaded = false, failed = false;
    const auto cLoaded = QObject::connect(mediaLoader, &MediaLoader::loaded, &loop,
                                          [&](const QImage&, const QString&) {
                                            loaded = true;
                                            loop.quit();
                                          });
    const auto cFailed = QObject::connect(mediaLoader, &MediaLoader::failed, &loop,
                                          [&](const QString& msg) {
                                            failed = true;
                                            if (why) *why = msg;
                                            loop.quit();
                                          });
    QTimer::singleShot(SOURCE_WAIT_MS, &loop, [&loop] { loop.quit(); });
    openSourceHere(src, frame, incognito);
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

