#include "ProjectsDialog.hpp"

#include "projectsRowChrome.hpp"
#include "fetchGuard.hpp"
#include "../../../support/rowWork.hpp"

#include <QBuffer>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QPointer>
#include <QUrl>
#include <QListWidget>
#include <QListWidgetItem>

// Row thumbnails: the server/source fetches and what lands on a row.

namespace stencil::gui {

  namespace fetchGuard = stencil::net::fetchGuard;

  namespace {
    // Decoded on the pool straight at the row's 320px, never full size on this thread.
    void decodeThumb(QObject* ctx, const QByteArray& bytes, std::function<void(QImage)> done) {
      support::runOnPool<QImage>(ctx, [bytes] {
        QByteArray data = bytes;
        QBuffer buf(&data);
        buf.open(QIODevice::ReadOnly);
        QImageReader reader(&buf);
        const QSize full = reader.size();
        if (full.isValid() && (full.width() > 320 || full.height() > 320))
          reader.setScaledSize(full.scaled(320, 320, Qt::KeepAspectRatio));
        return reader.read();
      }, std::move(done));
    }
  }  // namespace

  void ProjectsDialog::commitRowEdit(
      const QString& id, const QString& server,
      const std::function<void(Project&)>& mutate,
      const std::function<void(stencil::net::ServerClient*, qint64,
                               std::function<void(bool, qint64)>)>& push,
      const std::function<void(stencil::net::ServerProject&)>& cache) {
    if (server.isEmpty()) {
      for (auto& p : projects)
        if (QString::fromStdString(p.meta.id) == id) { mutate(p); break; }
      fileStore::saveProjects(projects);
      refresh();
      return;
    }
    stencil::net::ServerClient* c = connections ? connections->find(server) : nullptr;
    if (!c) return;
    qint64 version = 0;
    for (const auto& sp : remote)
      if (sp.id == id && sp.serverUrl == server) { version = sp.version; break; }
    QPointer<ProjectsDialog> self(this);
    push(c, version, [this, self, id, server, cache](bool ok, qint64 newVersion) {
      if (!self || !ok) return;
      for (auto& sp : remote)
        if (sp.id == id && sp.serverUrl == server) { cache(sp); sp.version = newVersion; break; }
      refresh();
    });
  }

  void ProjectsDialog::refreshRemote() {
    if (!connections || remoteBusy) return;
    remoteBusy = true;
    // Async cross-connection list (no nested event loop): the dialog stays responsive while the
    // server(s) respond; the rows populate when the merged listing resolves.
    QPointer<ProjectsDialog> self(this);
    connections->sharedProjectsAsync([this, self](QVector<stencil::net::ServerProject> ps) {
      if (!self) return;
      remote = ps;
      remoteBusy = false;
      remoteLoaded = true;  // first listing resolved → drop the loading placeholder
      refresh();
    });
  }

  QPixmap ProjectsDialog::remoteThumb(const stencil::net::ServerProject& sp) {
    if (!connections) return {};
    const QString key = QString("%1|%2|%3").arg(sp.serverUrl, sp.id).arg(sp.version);
    if (const QPixmap* cached = remoteThumbs.find(key)) return *cached;
    // Not cached — fetch asynchronously (result → original → source URL) so the dialog never
    // blocks on the network; the placeholder shows now and the icon swaps in on arrival.
    fetchServerThumbAsync(key, sp);
    return {};
  }

  // Preview fallback order: rendered `result` -> `original` -> the `source` web URL. Downloads bind
  // to the connection's network manager, which outlives this dialog, so a QPointer guards callbacks.
  void ProjectsDialog::fetchServerThumbAsync(const QString& key,
                                             const stencil::net::ServerProject& sp) {
    if (thumbInFlight.contains(key)) return;  // already downloading this version
    stencil::net::ServerClient* c = connections ? connections->find(sp.serverUrl) : nullptr;
    if (!c) {
      fetchSourceThumbAsync(key, sp);  // no live client — try the source URL directly
      return;
    }
    thumbInFlight.insert(key);
    const QString id = sp.id;
    const QString serverUrl = sp.serverUrl;
    const stencil::net::ServerProject spCopy = sp;
    QPointer<ProjectsDialog> self(this);
    // No rendered result — fall back to the uploaded original, then to the `source` web URL.
    auto original = [this, self, key, id, serverUrl, spCopy, c] {
      c->downloadFileAsync(id, "original", [this, self, key, id, serverUrl, spCopy](bool ok,
                                                                                    QByteArray bytes) {
        if (!self) return;
        auto source = [this, key, spCopy] {
          thumbInFlight.remove(key);
          fetchSourceThumbAsync(key, spCopy);
        };
        if (!ok || bytes.isEmpty()) return source();
        decodeThumb(this, bytes, [this, key, id, serverUrl, source](QImage img) {
          if (img.isNull()) return source();
          thumbInFlight.remove(key);
          applyRemoteThumb(key, id, serverUrl, img);
        });
      });
    };
    c->downloadFileAsync(id, "result", [this, self, key, id, serverUrl, original](bool ok,
                                                                                  QByteArray bytes) {
      if (!self) return;
      if (!ok || bytes.isEmpty()) return original();
      decodeThumb(this, bytes, [this, key, id, serverUrl, original](QImage img) {
        if (img.isNull()) return original();
        thumbInFlight.remove(key);
        applyRemoteThumb(key, id, serverUrl, img);
      });
    });
  }

  void ProjectsDialog::fetchSourceThumbAsync(const QString& key,
                                             const stencil::net::ServerProject& sp) {
    const QUrl u(sp.source);
    if (!u.isValid() || (u.scheme() != "http" && u.scheme() != "https")) {
      remoteThumbs.insert(key, QPixmap());  // nothing to fetch — cache the miss
      return;
    }
    if (thumbInFlight.contains(key)) return;  // already downloading this version
    thumbInFlight.insert(key);
    const QString id = sp.id;
    const QString serverUrl = sp.serverUrl;
    // This URL rides in on a SHARED project record, so it is untrusted: the STRICT guard, a capped
    // body, no redirect. A refusal caches like any other miss, and is never retried.
    fetchGuard::get(this, u, /*strict=*/true,
                    [this, key, id, serverUrl](const QByteArray& bytes, const QString&) {
                      decodeThumb(this, bytes, [this, key, id, serverUrl](QImage img) {
                        thumbInFlight.remove(key);
                        applyRemoteThumb(key, id, serverUrl, img);
                      });
                    });
  }

  void ProjectsDialog::applyRemoteThumb(const QString& key, const QString& id,
                                        const QString& serverUrl, const QImage& img) {
    QPixmap pm;
    if (!img.isNull())
      pm = QPixmap::fromImage(img.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    remoteThumbs.insert(key, pm);  // cache even a miss so we don't refetch every tick
    if (pm.isNull()) return;
    // Swap the placeholder for the picture on the live row (found by id+server, so a list rebuild
    // between request and response can't target a stale item).
    for (int i = 0; i < list->count(); ++i) {
      QListWidgetItem* it = list->item(i);
      if (it->data(Qt::UserRole).toString() == id &&
          it->data(Qt::UserRole + 1).toString() == serverUrl) {
        it->setIcon(QIcon(squareThumb(pm, 112)));   // uniform square row icon (cover)
        it->setData(Qt::UserRole + 2, pm);          // full-aspect source for hover-magnify
        break;
      }
    }
  }

  void ProjectsDialog::setLocalThumb(const QString& id, const QPixmap& thumb) {
    if (thumb.isNull()) return;
    thumbs.insert(id, thumb);
    for (int i = 0; list && i < list->count(); ++i) {
      QListWidgetItem* it = list->item(i);
      if (it->data(Qt::UserRole).toString() == id && it->data(Qt::UserRole + 1).toString().isEmpty()) {
        it->setIcon(QIcon(squareThumb(thumb, 112)));
        it->setData(Qt::UserRole + 2, thumb);
        break;
      }
    }
  }

}  // namespace stencil::gui
