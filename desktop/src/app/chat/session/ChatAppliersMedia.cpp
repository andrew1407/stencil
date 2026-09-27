#include "MainWindow.hpp"
#include "ChatAppliers.hpp"
#include "ChatSessionController.hpp"
#include "mainWindowHelpers.hpp"
#include "guiHelpers.hpp"   // confirmYesNo()
#include "MediaLoader.hpp"
#include "PlanAwait.hpp"
#include "Notifications.hpp"
#include "RemoteSession.hpp"
#include "ServerClient.hpp"
#include "SiblingWindows.hpp"

#include <QDir>

// The assistant's video and image appliers (llm-contract.md): offering a clip to the linked server,
// pulling frames out of it, and filing a picture as a new local project.

namespace stencil::gui {

  void ChatAppliers::offerChatVideoUpload(const QString& path) {
    // Optional server storage with kind "video" (contract §8).
    const auto& link = w.remote.session->getLink();
    if (link.address.isEmpty() || !w.remote.connections) return;
    auto* c = w.remote.connections->find(link.address);
    if (!c) return;
    const QString host = QUrl(link.address).host();
    if (!confirmYesNo(&w, "Upload video",
                      QStringLiteral("Store this video with the shared project on %1?").arg(host)))
      return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      w.notify->error(QStringLiteral("Could not read the video file"));
      return;
    }
    const QByteArray bytes = f.readAll();
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext.isEmpty()) ext = QStringLiteral("mp4");
    QPointer<MainWindow> self(&w);
    c->uploadFileAsync(link.id, QStringLiteral("video"), bytes, ext, 0, 0,
                       [self](bool ok) {
                         if (!self) return;
                         if (ok) self->notify->success("Video uploaded to the server");
                         else self->notify->error("Video upload failed");
                       });
  }

  // Sequential seeks, answered once the last lands (the MediaLoader timeout backstops a stuck
  // seek). A close lapses it as a success with no frames, so the plan runs on to its end.
  void ChatAppliers::chatExtractFramesThen(const QVector<int>& indices, llm::OpDone done) {
    if (w.chatSession->chatVideoPath.isEmpty())
      return done(false, QStringLiteral("frame: no video attached"));
    if (!w.chatSession->chatMedia) w.chatSession->chatMedia = new MediaLoader(&w);
    const auto frames = std::make_shared<QList<QImage>>();
    PlanAwait::start(
        w, w.pop,
        [this, indices, frames, done](bool ok, const QString& error) {
          if (!ok) return done(false, QStringLiteral("frame: %1").arg(error));
          fileExtractedFrames(*frames, indices);
          done(true, QString());
        },
        [&](PlanAwait* await) {
          w.chatSession->chatMedia->extractFrames(
              w.chatSession->chatVideoPath, QList<int>(indices.begin(), indices.end()),
              [guard = QPointer<PlanAwait>(await), frames](QList<QImage> f, QString e) {
                if (!guard || guard->isSettled()) return;
                *frames = std::move(f);
                guard->settle(e.isEmpty(), e);
              });
        },
        0, /*lapseOk=*/true);
  }

  void ChatAppliers::fileExtractedFrames(const QList<QImage>& frames, const QVector<int>& indices) {
    bool registryChanged = false;
    for (int i = 0; i < frames.size(); ++i)
      registryChanged =
          !addImageProjectEntry(frames.at(i), QStringLiteral("frame %1").arg(indices.at(i)),
                                /*deferRegistrySave=*/true)
               .isEmpty() ||
          registryChanged;
    // ONE registry write + dock-menu rebuild for the whole extraction.
    if (registryChanged) {
      fileStore::saveProjects(w.projectList);
      SiblingWindows::refreshDockMenu(w.projectList);
    }
    w.refreshActions();
    w.notify->success(QStringLiteral("Opened %1 video frame(s) as projects").arg(frames.size()));
  }

  QString ChatAppliers::addImageProjectEntry(const QImage& img, const QString& baseName,
                                           bool deferRegistrySave) {
    if (img.isNull() || w.incognito) return QString();  // incognito never persists
    Project pr;
    pr.meta.id = w.projectsStore.createId(nowMs(), makeSalt());
    // Unique-ify against the local list with the shared collision rules (checkProjectName → core validateName).
    QString name = baseName.isEmpty() ? QStringLiteral("variant") : baseName;
    {
      QString candidate = name;
      int i = 2;
      while (!w.checkProjectName(candidate, QString()).ok)
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
    w.projectList.push_back(pr);
    if (!deferRegistrySave) {
      fileStore::saveProjects(w.projectList);
      SiblingWindows::refreshDockMenu(w.projectList);
    }
    return QString::fromStdString(pr.meta.id);
  }
}  // namespace stencil::gui

