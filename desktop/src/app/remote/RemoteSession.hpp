#pragma once
#include <QObject>
#include <QString>
#include <functional>

namespace stencil::net {
  class ConnectionManager;
  class ServerClient;
}

namespace stencil::gui {

  class Notifications;

  // Bundled so binding/unbinding is one call; mirrors the browser's DrawingApp.remoteLink {
  // address, remoteId, version }.
  struct RemoteLink {
    QString address;
    QString id;
    QString name;
    // Kept in step with the server record.
    QString color;
    qint64 version = 0;
    void bind(const QString& a, const QString& i, const QString& n, const QString& c, qint64 v) {
      address = a; id = i; name = n; color = c; version = v;
    }
    void unbind() { address.clear(); id.clear(); name.clear(); color.clear(); version = 0; }
  };

  // A file write bumps the version by one; any more is a peer's edit, which the reload that
  // follows must still see, so only our own bump is adopted.
  inline void adoptOwnFileVersion(RemoteLink& link, qint64 before, qint64 now) {
    if (now == before + 1 && link.version == before) link.version = now;
  }

  // The server-project session domain: remote-link state, the ConnectionManager handle and the
  // version-guarded write helpers. The canvas-entangled CRUD stays on MainWindow.
  class RemoteSession : public QObject {
    Q_OBJECT
   public:
    RemoteSession(QObject* parent, Notifications* notify)
        : QObject(parent), notify(notify) {}

    // By reference so MainWindow sets fields in place and bind()/unbind() it wholesale.
    RemoteLink& getLink() { return link; }
    const RemoteLink& getLink() const { return link; }

    // Null until MainWindow lazily creates it.
    void setConnections(stencil::net::ConnectionManager* c) { connections = c; }
    stencil::net::ConnectionManager* getConnections() const { return connections; }

    const QString& address() const { return link.address; }
    const QString& id() const { return link.id; }
    qint64 version() const { return link.version; }

    // A save toasts when its outcome differs from the last one for this project, not per push.
    bool saveOutcomeChanged(bool ok);

    // The originalHash of the picture the canvas adopted from this link: a peer record with the
    // same one, over a canvas still showing it, is a layout-only edit. Empty never matches.
    void noteAdoptedOriginal(const QString& originalHash, qint64 imageKey);
    bool isAdoptedOriginal(const QString& originalHash, qint64 imageKey) const;

    // nullptr + `msg` notified when not connected.
    stencil::net::ServerClient* requireClient(
        const QString& url, const QString& msg = QStringLiteral("Not connected to that server"));

    // Async guarded PUT, re-reading `id`'s version before each attempt, up to 4 retries on a 409.
    // Loop state is heap-managed: a reply after `c` dies is a no-op; callers guard `done`'s captures.
    void putVersionGuardedAsync(
        stencil::net::ServerClient* c, const QString& id,
        std::function<void(qint64 version,
                           std::function<void(bool ok, qint64 newVersion, bool conflict)> cb)> put,
        std::function<void(bool ok, qint64 outVersion)> done);

   private:
    RemoteLink link;
    QString outcomeKey;
    int lastOutcome = -1;   // -1 none yet, 0 failed, 1 saved
    QString adoptedKey;
    QString adoptedHash;
    qint64 adoptedImageKey = 0;
    stencil::net::ConnectionManager* connections = nullptr;
    Notifications* notify;
  };

}  // namespace stencil::gui
