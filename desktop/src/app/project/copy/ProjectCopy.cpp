// The copy's request rules and its one run: read the source, settle what can be honoured, name it
// past every taken name (the server's too for a server copy), then make it and open it.
#include "ProjectCopy.hpp"
#include "MainWindow.hpp"
#include "Notifications.hpp"
#include "ServerClient.hpp"

#include <QPointer>
#include <QStringList>

namespace stencil::gui {

  Project scopedCopy(const CopySource& src, support::CopyScope what, const std::string& id,
                     const QString& name, long long now) {
    const core::ProjectMeta& m = src.meta;
    Project pr;
    pr.meta.id = id;
    pr.meta.name = name.toStdString();
    pr.meta.createdAt = pr.meta.updatedAt = now;
    pr.meta.expiresAt = core::ProjectsStore::addPeriod(now, core::ProjectsStore::DEFAULT_PERIOD);
    pr.meta.hasImage = true;
    pr.meta.source = m.source;
    pr.meta.resource = m.resource;
    pr.meta.blank = m.blank;
    pr.meta.blankColor = m.blankColor;
    if (what == support::COPY_IMAGE) return pr;
    pr.meta.imageW = m.imageW;
    pr.meta.imageH = m.imageH;
    pr.meta.lineLengthCm = m.lineLengthCm;
    pr.lines = src.lines;
    pr.cropRect = src.crop;
    pr.rotationQuarters = src.quarters;
    if (what != support::COPY_PROJECT) return pr;
    pr.meta.color = m.color;
    pr.meta.description = m.description;
    pr.meta.keywords = m.keywords;
    pr.meta.expiresAt = m.expiresAt;
    pr.meta.refreshPeriod = m.refreshPeriod;
    pr.meta.autoRefresh = m.autoRefresh;
    pr.chat = src.chat;
    return pr;
  }

  CopyRequest ProjectCopy::settle(CopyRequest req, bool serverSource, QString* note) {
    QStringList notes;
    if (req.incognito && req.open == COPY_OPEN_NONE) {
      req.incognito = false;
      notes << QStringLiteral("an incognito copy must be opened — saved it instead");
    }
    if (req.incognito && serverSource && !req.local) {
      req.incognito = false;
      notes << QStringLiteral("a server copy cannot be incognito — made it on the server");
    }
    if (note) *note = notes.join(QStringLiteral("; "));
    return req;
  }

  void ProjectCopy::run(const CopyRequest& req, std::function<void(bool, QString, QString)> done) {
    QPointer<MainWindow> self(&w);
    readSource(req, [this, self, req, done](std::optional<CopySource> read, QString error) {
      if (!self) return;
      if (!read) {
        w.notify->error(QStringLiteral("Could not make the copy — %1").arg(error));
        if (done) done(false, QString(), error);
        return;
      }
      QString note;
      const CopyRequest opts = settle(req, !read->server.isEmpty(), &note);
      const bool onServer = !read->server.isEmpty() && !opts.local;
      const auto reply = [done, note](bool ok, const QString& id) { if (done) done(ok, id, note); };
      const auto make = [this, self, opts, onServer, src = *read, reply](const std::vector<core::ProjectMeta>& taken) {
        if (!self) return;
        const QString name = copyNameAmong(taken, src.meta.name);
        if (opts.incognito) {
          openIncognito(src, opts.what, opts.open);
          reply(true, QString());
          return;
        }
        if (onServer) {
          createOnServer(src, opts.what, name, [this, self, open = opts.open, server = src.server, reply](bool ok, QString id) {
            if (!self || !ok) return reply(false, id);
            openSaved(open, server, id, [reply, id] { reply(true, id); });
          });
          return;
        }
        const QString id = createLocal(src, opts.what, name);
        if (id.isEmpty()) return reply(false, id);
        openSaved(opts.open, QString(), id, [reply, id] { reply(true, id); });
      };
      auto* c = onServer && w.remote.connections ? w.remote.connections->find(read->server) : nullptr;
      if (!c) { make({}); return; }
      // "-copy(N)" numbers past the server's own names too (browser copy/server.js serverNames).
      c->listProjectsAsync([make](bool ok, QVector<stencil::net::ServerProject> list) {
        std::vector<core::ProjectMeta> taken;
        for (const auto& p : ok ? list : QVector<stencil::net::ServerProject>{}) {
          core::ProjectMeta m;
          m.id = "remote:" + p.id.toStdString();
          m.name = p.name.toStdString();
          taken.push_back(m);
        }
        make(taken);
      });
    });
  }

  QString ProjectCopy::copyNameAmong(const std::vector<core::ProjectMeta>& extra, const std::string& name) const {
    std::vector<core::ProjectMeta> metas = extra;
    for (const Project& pr : w.projectList) metas.push_back(pr.meta);
    core::ProjectsStore store;
    store.load(std::move(metas));
    return QString::fromStdString(store.copySuffixName(name));
  }

}  // namespace stencil::gui
