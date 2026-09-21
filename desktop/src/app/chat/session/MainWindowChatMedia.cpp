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
#include "../../../support/localPath.hpp"

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

  void MainWindow::onChatVideoAttached(const QString& path) {
    chatVideoPath = path;
    chatVideoFrames = 0;
    if (!chatMedia) chatMedia = new MediaLoader(this);
    // Videos are NEVER sent to the LLM (contract §7): the first frame goes, the timeline feeds the system suffix.
    QPointer<MainWindow> self(this);
    chatMedia->extractFrames(path, {0}, [self](QList<QImage> frames, QString err) {
      if (!self) return;
      if (!err.isEmpty()) {
        self->notify->error(QStringLiteral("Video preview failed: %1").arg(err));
        return;
      }
      self->chatVideoFrames = self->chatMedia->frameCount();
      if (!frames.isEmpty()) self->chatDock->addAttachmentImage(frames.first());
      self->notify->info(
          QStringLiteral("Video frame attached — the video itself is never sent"));
    });
    offerChatVideoUpload(path);
  }

  void MainWindow::offerChatVideoUpload(const QString& path) {
    // Optional server storage with kind "video" (contract §8).
    const auto& link = remoteSession->getLink();
    if (link.address.isEmpty() || !connections) return;
    auto* c = connections->find(link.address);
    if (!c) return;
    const QString host = QUrl(link.address).host();
    if (!confirmYesNo(this, "Upload video",
                      QStringLiteral("Store this video with the shared project on %1?").arg(host)))
      return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      notify->error(QStringLiteral("Could not read the video file"));
      return;
    }
    const QByteArray bytes = f.readAll();
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext.isEmpty()) ext = QStringLiteral("mp4");
    QPointer<MainWindow> self(this);
    c->uploadFileAsync(link.id, QStringLiteral("video"), bytes, ext, 0, 0,
                       [self](bool ok) {
                         if (!self) return;
                         if (ok) self->notify->success("Video uploaded to the server");
                         else self->notify->error("Video upload failed");
                       });
  }

  bool MainWindow::chatExtractFrames(const QVector<int>& indices, QString* err) {
    if (chatVideoPath.isEmpty()) {
      if (err) *err = QStringLiteral("frame: no video attached");
      return false;
    }
    if (!chatMedia) chatMedia = new MediaLoader(this);
    // Sequential seeks are async; block on a local loop (the MediaLoader timeout backstops a stuck seek).
    // `finished` guards the synchronous-failure path, or exec() on a never-started loop runs forever.
    QEventLoop loop;
    QList<QImage> frames;
    QString error;
    bool finished = false;
    chatMedia->extractFrames(chatVideoPath, QList<int>(indices.begin(), indices.end()),
                              [&](QList<QImage> f, QString e) {
                                frames = std::move(f);
                                error = std::move(e);
                                finished = true;
                                loop.quit();
                              });
    if (!finished) loop.exec();
    if (!error.isEmpty()) {
      if (err) *err = QStringLiteral("frame: %1").arg(error);
      return false;
    }
    bool registryChanged = false;
    for (int i = 0; i < frames.size(); ++i)
      registryChanged =
          !addImageProjectEntry(frames.at(i), QStringLiteral("frame %1").arg(indices.at(i)),
                                /*deferRegistrySave=*/true)
               .isEmpty() ||
          registryChanged;
    // ONE registry write + dock-menu rebuild for the whole extraction.
    if (registryChanged) {
      fileStore::saveProjects(projectList);
      refreshDockMenu();
    }
    refreshActions();
    notify->success(QStringLiteral("Opened %1 video frame(s) as projects").arg(frames.size()));
    return true;
  }

  QString MainWindow::addImageProjectEntry(const QImage& img, const QString& baseName,
                                           bool deferRegistrySave) {
    if (img.isNull() || incognito) return QString();  // incognito never persists
    Project pr;
    pr.meta.id = projectsStore.createId(nowMs(), makeSalt());
    // Unique-ify against the local list with the shared collision rules (checkProjectName → core validateName).
    QString name = baseName.isEmpty() ? QStringLiteral("variant") : baseName;
    {
      QString candidate = name;
      int i = 2;
      while (!checkProjectName(candidate, QString()).ok)
        candidate = QStringLiteral("%1 %2").arg(name).arg(i++);
      name = candidate;
    }
    pr.meta.name = name.toStdString();
    pr.meta.createdAt = pr.meta.updatedAt = nowMs();
    pr.meta.expiresAt = core::ProjectsStore::addPeriod(pr.meta.updatedAt,
                                                       core::ProjectsStore::DEFAULT_PERIOD);
    const QString imgDir = fileStore::stateDir() + "/images";
    QDir().mkpath(imgDir);
    const QString path = imgDir + "/" + QString::fromStdString(pr.meta.id) + ".png";
    if (!img.save(path, "PNG")) return QString();
    pr.imagePath = path;
    pr.meta.hasImage = true;
    pr.meta.imageW = img.width();
    pr.meta.imageH = img.height();
    projectList.push_back(pr);
    if (!deferRegistrySave) {
      fileStore::saveProjects(projectList);
      refreshDockMenu();
    }
    return QString::fromStdString(pr.meta.id);
  }
}  // namespace stencil::gui

