// Where a copy is written (browser core/project/copy/local.js + server.js): a fresh local row over
// its own image file, never the source's path; or a new project on the source's server from the
// original's bytes, its layout PUT for layout and up, its colour, words and chat for a whole project.
#include "ProjectCopy.hpp"
#include "MainWindow.hpp"
#include "Notifications.hpp"
#include "ServerClient.hpp"
#include "SiblingWindows.hpp"
#include "projectTransferParts.hpp"   // nowMs, makeSalt, pngBytes

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>

#include <memory>
#include <tuple>

namespace stencil::gui {

  namespace {
    struct Upload {
      QByteArray bytes;
      QString ext;
      QSize size;
    };
    // What a reader makes of the bytes: their format (the file's extension) and their size.
    std::pair<QString, QSize> probe(const QByteArray& bytes) {
      QByteArray copy = bytes;
      QBuffer buf(&copy);
      buf.open(QIODevice::ReadOnly);
      QImageReader reader(&buf);
      const QString f = QString::fromLatin1(reader.format()).toLower();
      return {f.isEmpty() ? QStringLiteral("png") : (f == QLatin1String("jpeg") ? QStringLiteral("jpg") : f),
              reader.size()};
    }
    Upload uploadOf(const CopySource& src) {
      Upload up;
      up.bytes = src.bytes;
      if (up.bytes.isEmpty() && !src.imagePath.isEmpty()) {
        QFile f(src.imagePath);
        if (f.open(QIODevice::ReadOnly)) up.bytes = f.readAll();
      }
      if (up.bytes.isEmpty() && !src.image.isNull()) up.bytes = pngBytes(src.image);
      std::tie(up.ext, up.size) = probe(up.bytes);
      return up;
    }

    using Step = std::function<void(qint64 version, std::function<void(qint64)> next)>;
    // Guarded PUTs in order, each on the version the last one left.
    void runSteps(std::shared_ptr<const std::vector<Step>> steps, std::size_t i, qint64 version,
                  std::function<void()> done) {
      if (i >= steps->size()) return done();
      (*steps)[i](version, [steps, i, done](qint64 next) { runSteps(steps, i + 1, next, done); });
    }
    void pushOwnMeta(stencil::net::ServerClient* c, const QString& id, qint64 version, const Project& pr,
                     std::function<void()> done) {
      auto steps = std::make_shared<std::vector<Step>>();
      const auto keep = [](qint64 v, const std::function<void(qint64)>& next) {
        return [v, next](bool ok, qint64 nv, bool) { next(ok ? nv : v); };
      };
      const QString color = QString::fromStdString(pr.meta.color);
      const QString description = QString::fromStdString(pr.meta.description);
      QStringList keywords;
      for (const auto& k : pr.meta.keywords) keywords << QString::fromStdString(k);
      if (!color.isEmpty())
        steps->push_back([=](qint64 v, auto next) { c->updateProjectColorAsync(id, color, v, keep(v, next)); });
      if (!description.isEmpty())
        steps->push_back([=](qint64 v, auto next) { c->updateProjectDescriptionAsync(id, description, v, keep(v, next)); });
      if (!keywords.isEmpty())
        steps->push_back([=](qint64 v, auto next) { c->updateProjectKeywordsAsync(id, keywords, v, keep(v, next)); });
      if (!pr.chat.isEmpty()) {
        const QByteArray chat = QJsonDocument(pr.chat).toJson(QJsonDocument::Compact);
        steps->push_back([=](qint64 v, auto next) {
          c->uploadFileAsync(id, QStringLiteral("chat"), chat, QStringLiteral("json"), 0, 0, [v, next](bool) { next(v); });
        });
      }
      runSteps(steps, 0, version, std::move(done));
    }
  }  // namespace

  QString ProjectCopy::createLocal(const CopySource& src, support::CopyScope what, const QString& name) {
    const std::string id = w.projectsStore.createId(nowMs(), makeSalt());
    Project pr = scopedCopy(src, what, id, name, nowMs());
    const QString dir = fileStore::stateDir() + "/images";
    QDir().mkpath(dir);
    const QString stem = dir + "/" + QString::fromStdString(id) + ".";
    QString path;
    if (!src.imagePath.isEmpty() && QFileInfo::exists(src.imagePath)) {
      const QString ext = QFileInfo(src.imagePath).suffix().toLower();
      path = stem + (ext.isEmpty() ? QStringLiteral("png") : ext);
      if (!QFile::copy(src.imagePath, path)) path.clear();
    } else if (!src.bytes.isEmpty()) {
      path = stem + probe(src.bytes).first;
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly) || f.write(src.bytes) != src.bytes.size()) path.clear();
    } else if (!src.image.isNull()) {
      path = stem + QStringLiteral("png");
      if (!src.image.save(path, "PNG")) path.clear();
    }
    if (path.isEmpty()) {
      w.notify->error(QStringLiteral("Could not write the copy's image to local storage"));
      return QString();
    }
    pr.imagePath = path;
    w.projectList.push_back(pr);
    fileStore::saveProjects(w.projectList);
    w.refreshActions();
    SiblingWindows::refreshDockMenu(w.projectList);
    return QString::fromStdString(id);
  }

  QJsonObject ProjectCopy::layoutOf(const CopySource& src, support::CopyScope what, int width, int height) const {
    if (what == support::COPY_IMAGE)
      return fileStore::buildLayoutJson(width, height, {}, QStringLiteral("none"), w.settings.filterColor, {}, 0,
                                        w.currentLayoutMeta());
    if (!src.layout.isEmpty()) return src.layout;
    return fileStore::buildLayoutJson(width, height, src.lines, w.settings.imageFilter, w.settings.filterColor,
                                      src.crop, src.quarters, w.currentLayoutMeta(), src.mirrored);
  }

  void ProjectCopy::createOnServer(const CopySource& src, support::CopyScope what, const QString& name,
                                   std::function<void(bool, QString)> done) {
    stencil::net::ServerClient* c = w.remote.connections ? w.remote.connections->find(src.server) : nullptr;
    const Upload up = uploadOf(src);
    if (!c || up.bytes.isEmpty()) {
      w.notify->error(c ? QStringLiteral("Could not read the copy's image") : QStringLiteral("Not connected to %1").arg(src.server));
      return done(false, QString());
    }
    const Project pr = scopedCopy(src, what, std::string(), name, nowMs());
    const QJsonObject layout = layoutOf(src, what, up.size.width(), up.size.height());
    const auto fail = [this, c, done](const QString& stage) {
      w.notify->error(QStringLiteral("%1 — %2").arg(stage, c->lastError()));
      done(false, QString());
    };
    QJsonObject extra;   // the browser's createRemoteProject sends these only when set
    if (pr.meta.blank && !pr.meta.blankColor.empty()) extra.insert("blankColor", QString::fromStdString(pr.meta.blankColor));
    if (what == support::COPY_PROJECT && pr.meta.expiresAt > 0) extra.insert("expiresAt", static_cast<double>(pr.meta.expiresAt));
    c->createProjectAsync(name, QString::fromStdString(pr.meta.source), QString::fromStdString(pr.meta.resource), true,
                          up.size.width(), up.size.height(), [=](bool ok, QString id, qint64) {
      if (!ok) return fail(QStringLiteral("Could not create the copy on the server"));
      c->uploadFileAsync(id, QStringLiteral("original"), up.bytes, up.ext, up.size.width(), up.size.height(), [=](bool uok) {
        if (!uok) return fail(QStringLiteral("Created, but the image upload failed"));
        if (what == support::COPY_IMAGE) return done(true, id);
        // The upload bumped the version, and the layout PUT is guarded: send the one it left.
        c->getProjectAsync(id, [=](bool gok, stencil::net::ServerProject meta, QJsonObject) {
          if (!gok) return fail(QStringLiteral("Created, but the layout was not saved"));
          c->updateProjectAsync(id, name, layout, meta.version, [=](bool pok, qint64 version, bool) {
            if (!pok) return fail(QStringLiteral("Created, but the layout was not saved"));
            if (what != support::COPY_PROJECT) return done(true, id);
            pushOwnMeta(c, id, version, pr, [done, id] { done(true, id); });
          });
        });
      });
    });
  }

}  // namespace stencil::gui
