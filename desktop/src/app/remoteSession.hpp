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

  // ── RemoteLink: the current session's server linkage ────────────────────────
  // Value bundle (empty address = a purely-local project). Bundled so binding/unbinding is a
  // single call that can't set-or-clear only some of the fields. Mirrors the browser's
  // DrawingApp.remoteLink { address, remoteId, version }.
  struct RemoteLink {
    QString address;
    QString id;
    QString name;
    // The linked server project's accent colour ("#rrggbb" or empty). Kept in step with the
    // server record so a server session paints its name like a local one.
    QString color;
    qint64 version = 0;
    void bind(const QString& a, const QString& i, const QString& n, const QString& c, qint64 v) {
      address = a; id = i; name = n; color = c; version = v;
    }
    void unbind() { address.clear(); id.clear(); name.clear(); color.clear(); version = 0; }
  };

  // ── RemoteSession: the server-project session domain ────────────────────────
  // Extracted from MainWindow. Owns the current session's remote-link state and the window's
  // ConnectionManager handle, and hosts the version-guarded write helpers (requireClient /
  // putVersionGuarded). The live-sync controller (RemoteSyncController) composes it DIRECTLY —
  // reading link/connection state through its accessors instead of MainWindow function hooks.
  //
  // NOTE (deferred): the canvas/UI-entangled CRUD (createServerProject / saveToServer /
  // openServerProject) still lives on MainWindow — those methods drive the canvas, notifications
  // and toolbar, so moving them would just relocate the coupling. They call back into this
  // object's helpers + link state. All are now async (non-blocking REST via ServerClient's *Async
  // surface); putVersionGuardedAsync is the shared version-guarded write.
  class RemoteSession : public QObject {
    Q_OBJECT
   public:
    RemoteSession(QObject* parent, Notifications* notify);

    // The remote link, exposed by reference so MainWindow keeps setting individual fields
    // (version bumps, rename, colour) in place, and bind()/unbind() it wholesale.
    RemoteLink& link() { return link_; }
    const RemoteLink& link() const { return link_; }

    // The window's ConnectionManager (set once MainWindow lazily creates it; null before then).
    void setConnections(stencil::net::ConnectionManager* c) { connections_ = c; }
    stencil::net::ConnectionManager* connections() const { return connections_; }

    // Convenience accessors used by the live-sync controller (avoid ->link(). churn there).
    const QString& address() const { return link_.address; }
    const QString& id() const { return link_.id; }
    qint64 version() const { return link_.version; }

    // Resolve the live ServerClient for `url` on the window's ConnectionManager, notifying `msg`
    // and returning nullptr when it isn't connected. The recurring find-or-notify guard shared
    // by the server CRUD methods.
    stencil::net::ServerClient* requireClient(
        const QString& url, const QString& msg = QStringLiteral("Not connected to that server"));

    // Async version-guarded server write with a bounded conflict retry (no nested event loop).
    // `put(version, cb)` performs one
    // guarded PUT and reports (ok, newVersion, conflict) via `cb`; the loop re-reads `id`'s current
    // version before each attempt (identity conflict policy) and retries up to 4 times on a 409.
    // The final (ok, winning-version) is delivered to `done`. Loop state is heap-managed, so a reply
    // finishing after `c` is destroyed is a safe no-op; callers must guard `done`'s captures for
    // their own lifetime. Shared by commitProjectName / setProjectColorById.
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
