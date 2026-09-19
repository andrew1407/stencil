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

  // The server-project session domain: remote-link state, the ConnectionManager handle and the
  // version-guarded write helpers. The canvas-entangled CRUD stays on MainWindow.
  class RemoteSession : public QObject {
    Q_OBJECT
   public:
    RemoteSession(QObject* parent, Notifications* notify);

    // By reference so MainWindow sets fields in place and bind()/unbind() it wholesale.
    RemoteLink& link() { return link_; }
    const RemoteLink& link() const { return link_; }

    // Null until MainWindow lazily creates it.
    void setConnections(stencil::net::ConnectionManager* c) { connections_ = c; }
    stencil::net::ConnectionManager* connections() const { return connections_; }

    const QString& address() const { return link_.address; }
    const QString& id() const { return link_.id; }
    qint64 version() const { return link_.version; }

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
    RemoteLink link_;
    stencil::net::ConnectionManager* connections_ = nullptr;
    Notifications* notify_;
  };

}  // namespace stencil::gui
