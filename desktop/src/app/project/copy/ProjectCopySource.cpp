// What a copy is taken from (browser core/project/copy/source.js): the live editor as it stands,
// a stored row, or a server-only row fetched with its original and, for a whole project, its chat.
#include "ProjectCopy.hpp"
#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "ChatSessionController.hpp"
#include "RemoteSession.hpp"
#include "ServerClient.hpp"
#include "mainWindowShared.hpp"

#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>

namespace stencil::gui {

  namespace {
    // A server row as a source: its record's meta, its layout's lines and view, its original's bytes.
    CopySource serverSource(const stencil::net::ServerProject& p, const QJsonObject& layout,
                            const QByteArray& bytes, const QString& serverUrl) {
      CopySource src;
      src.meta.name = (p.name.isEmpty() ? QStringLiteral("Untitled") : p.name).toStdString();
      src.meta.color = p.color.toStdString();
      src.meta.description = p.description.toStdString();
      for (const QString& k : p.keywords) src.meta.keywords.push_back(k.toStdString());
      src.meta.blank = !p.blankColor.isEmpty();
      src.meta.blankColor = p.blankColor.toStdString();
      src.meta.expiresAt = p.expiresAt;
      src.meta.source = p.source.toStdString();
      src.meta.resource = p.resource.toStdString();
      src.meta.imageW = p.imageW;
      src.meta.imageH = p.imageH;
      int lw = 0, lh = 0;
      src.lines = fileStore::parseLayoutJson(layout, lw, lh, &src.crop, &src.quarters);
      src.layout = layout;
      src.bytes = bytes;
      src.server = serverUrl;
      return src;
    }
  }  // namespace

  void ProjectCopy::readSource(const CopyRequest& req,
                               std::function<void(std::optional<CopySource>, QString)> done) {
    if (!req.serverUrl.isEmpty()) return readServerRow(req, std::move(done));
    if (req.id.isEmpty() || req.id == w.activeProjectId) {
      if (!w.canvas->hasImage()) return done(std::nullopt, QStringLiteral("there is no image to copy"));
      return done(liveSource(), QString());
    }
    const Project* pr = w.findProject(req.id.toStdString());
    if (!pr) return done(std::nullopt, QStringLiteral("project not found"));
    CopySource src;
    src.meta = pr->meta;
    src.imagePath = pr->imagePath;
    src.lines = pr->lines;
    src.crop = pr->cropRect;
    src.quarters = pr->rotationQuarters;
    src.chat = pr->chat;
    // A blank project may have no file: it is its fill (ProjectFlows::openInAnotherAppFor).
    if (pr->imagePath.isEmpty() && pr->meta.blank && pr->meta.imageW > 0 && pr->meta.imageH > 0) {
      src.image = QImage(pr->meta.imageW, pr->meta.imageH, QImage::Format_ARGB32);
      const QColor fill(QString::fromStdString(pr->meta.blankColor));
      src.image.fill(fill.isValid() ? fill : QColor(Qt::white));
    } else if (!QFileInfo::exists(pr->imagePath)) {
      return done(std::nullopt, QStringLiteral("that project has no image to copy"));
    }
    done(src, QString());
  }

  void ProjectCopy::readServerRow(const CopyRequest& req,
                                  std::function<void(std::optional<CopySource>, QString)> done) {
    stencil::net::ServerClient* c = w.remote.connections ? w.remote.connections->find(req.serverUrl) : nullptr;
    if (!c) return done(std::nullopt, QStringLiteral("not connected to %1").arg(req.serverUrl));
    QPointer<MainWindow> self(&w);
    const bool wantChat = req.what == support::COPY_PROJECT;
    c->getProjectAsync(req.id, [self, c, req, wantChat, done](bool ok, stencil::net::ServerProject p, QJsonObject layout) {
      if (!self) return;
      if (!ok) return done(std::nullopt, c->lastError());
      const auto withBytes = [self, c, req, wantChat, p, layout, done](QByteArray bytes) {
        if (!self) return;
        if (bytes.isEmpty()) return done(std::nullopt, QStringLiteral("the server project has no image"));
        CopySource src = serverSource(p, layout, bytes, req.serverUrl);
        if (!wantChat) return done(src, QString());
        c->downloadFileAsync(req.id, QStringLiteral("chat"), [self, src, done](bool cok, QByteArray chat) mutable {
          if (!self) return;
          if (cok) src.chat = QJsonDocument::fromJson(chat).object();
          done(src, QString());
        });
      };
      c->downloadFileAsync(req.id, QStringLiteral("original"), [self, p, withBytes](bool dok, QByteArray bytes) {
        if (!self) return;
        if (dok && !bytes.isEmpty()) return withBytes(bytes);
        // An extension-added project keeps only its web URL: fetched through the strict guard, as an open does.
        fetchUrlBytesAsync(self.data(), p.source, withBytes);
      });
    });
  }

  // The live editor under its saved row's meta, with what is on screen now (browser liveProjectSnapshot).
  CopySource ProjectCopy::liveSource() {
    CopySource src;
    const Project* pr = w.activeProjectId.isEmpty() ? nullptr : w.findProject(w.activeProjectId.toStdString());
    const auto& link = w.remote.session->getLink();
    if (pr) src.meta = pr->meta;
    else src.meta.color = link.color.toStdString();
    src.meta.name = liveName().toStdString();
    if (!w.docSource.currentSource.isEmpty()) src.meta.source = w.docSource.currentSource.toStdString();
    if (!w.docSource.currentResource.isEmpty()) src.meta.resource = w.docSource.currentResource.toStdString();
    src.meta.blank = !w.docSource.blankColor.isEmpty();
    src.meta.blankColor = w.docSource.blankColor.toStdString();
    w.parts.view.stampCanvasMeta(src.meta);
    src.imagePath = w.canvas->getImagePath();
    src.image = w.canvas->getOriginalImage();
    src.lines = w.canvas->allLines();
    src.crop = w.canvas->getCropRect();
    src.quarters = w.canvas->getRotationQuarters();
    src.chat = w.chatSession->buildActiveChatDoc();
    if (!link.address.isEmpty() && !link.id.isEmpty()) src.server = link.address;
    return src;
  }

  QString ProjectCopy::liveName() {
    const Project* pr = w.activeProjectId.isEmpty() ? nullptr : w.findProject(w.activeProjectId.toStdString());
    if (pr) return QString::fromStdString(pr->meta.name);
    const QString linked = w.remote.session->getLink().name;
    if (!linked.isEmpty()) return linked;
    const QString base = w.canvas->imageBaseName();
    return base.isEmpty() ? QStringLiteral("Untitled") : base;
  }

  // The name and server the confirmation asks about, before anything is read.
  QString ProjectCopy::sourceName(const CopyRequest& from, QString* server) {
    server->clear();
    if (!from.serverUrl.isEmpty()) {
      *server = from.serverUrl;
      return from.name.isEmpty() ? QStringLiteral("Untitled") : from.name;
    }
    if (!from.id.isEmpty() && from.id != w.activeProjectId) {
      const Project* pr = w.findProject(from.id.toStdString());
      return pr ? QString::fromStdString(pr->meta.name) : QStringLiteral("Untitled");
    }
    const auto& link = w.remote.session->getLink();
    if (!link.address.isEmpty() && !link.id.isEmpty()) *server = link.address;
    return liveName();
  }

}  // namespace stencil::gui
