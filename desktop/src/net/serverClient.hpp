#pragma once
// ── Collaboration-server client (desktop) ───────────────────────────────────
// Mirrors server/internal/protocol over REST using QNetworkAccessManager. The
// desktop deliberately uses Qt Network (already linked) rather than a WebSocket
// library; live editing uses a raw QTcpSocket NDJSON transport (see the server's
// TCP listener) so no third-party dependency is added. This header covers the
// REST surface (connect/list/create/upload/download) plus a small manager that
// holds multiple connections for one window.
#include "connectionStore.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

class QNetworkAccessManager;
class QNetworkRequest;

namespace stencil::net {

  // Project metadata mirrored from protocol.ProjectRecord (the fields the desktop
  // needs to list/open shared projects).
  struct ServerProject {
    QString id;
    QString name;
    // Per-project accent color ("#rrggbb" or empty = theme default). Mirrors
    // protocol.ProjectRecord.Color so the shared name colour survives a re-list.
    QString color;
    bool hasImage = false;
    int imageW = 0;
    int imageH = 0;
    QString source;
    QString resource;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    // Expiry (epoch ms; 0 = never). Mirrors protocol.ProjectRecord.ExpiresAt;
    // shown next to the created date. Server projects have none by default.
    qint64 expiresAt = 0;
    // Monotonic edit version (LWW guard); echoed back on PUT to detect a 409.
    qint64 version = 0;
    // Origin server (base origin) this record came from — stamped by
    // ConnectionManager::sharedProjects() so the UI can route open/save back to
    // the right connection (the desktop analogue of the browser's `serverUrl`).
    QString serverUrl;
  };

  // One connected server. The REST surface is ASYNCHRONOUS (non-blocking, driven by
  // QNetworkAccessManager): each *Async method kicks off the request and invokes its completion
  // on the GUI thread, so a slow/hostile server never freezes the UI. The initial connect/reconnect
  // handshake is the one synchronous path retained (ConnectionManager::connectTo uses it inline at
  // startup + in the connect dialog); on-failure completions set lastError().
  class ServerClient {
   public:
    // Connection status for the UI dot: Connecting (yellow) | Connected (green) |
    // Error (red).
    enum class Status { Connecting, Connected, Error };

    // Outcome of one guarded PUT (and of the guarded-write loop as a whole): the write
    // committed, hit a stale-version 409 (Conflict), or hard-failed for another reason.
    enum class GuardOutcome { Committed, Conflict, Failed };

    explicit ServerClient(const QString& url);
    ~ServerClient();

    // Normalize 'host:8090' / 'http://host:8090/' to a clean origin. Secure by default:
    // a bare host (no scheme) gets https, EXCEPT loopback hosts, which keep http (dev
    // servers run plaintext on localhost and the traffic never leaves the machine).
    static QString normalizeBase(const QString& raw);
    // True for a loopback/localhost host (127.0.0.0/8, ::1, "localhost", "*.localhost"),
    // where plaintext http is safe because the bytes never hit the network.
    static bool isLoopbackHost(const QString& host);
    // True when `base` would send the bearer token + image bytes in CLEARTEXT to a remote
    // host (scheme http and not loopback) — the UI warns on these.
    static bool isInsecureRemote(const QString& base);

    // Validate a supplied token (GET /projects) or issue a fresh one
    // (POST /auth/token). Returns true when the connection is usable. Synchronous: the initial
    // connect handshake stays inline (ConnectionManager::connectTo drives it at startup + in the
    // connect dialog); every other REST call below is async.
    bool connect(const QString& token = QString());

    const QString& base() const { return base_; }
    const QString& token() const { return token_; }
    const QString& lastError() const { return err_; }
    Status status() const { return status_; }

    // ── Async REST surface ───────────────────────────────────────────────────
    // Non-blocking: each kicks off the request and invokes `done` on the GUI thread
    // when the reply completes (no nested event loop, so a slow/hostile server never
    // freezes the UI or re-enters the app). Behaviour, error strings and 409→conflict
    // semantics match the REST wire contract. The callback captures the caller's
    // context — callers must guard it (QPointer) so a reply finishing after the caller
    // is destroyed is a safe no-op; a reply finishing after THIS client is destroyed is
    // already safe (the connection is bound to nam_, which dies with the client).
    void connectAsync(const QString& token, std::function<void(bool ok)> done);
    void reconnectAsync(std::function<void(bool ok)> done);
    void listProjectsAsync(std::function<void(bool ok, QVector<ServerProject> projects)> done);
    void createProjectAsync(const QString& name, const QString& source, const QString& resource,
                            bool hasImage, int w, int h,
                            std::function<void(bool ok, QString id, qint64 version)> done);
    void getProjectAsync(const QString& id,
                         std::function<void(bool ok, ServerProject meta, QJsonObject layout)> done);
    void updateProjectAsync(const QString& id, const QString& name, const QJsonObject& layout,
                            qint64 version,
                            std::function<void(bool ok, qint64 newVersion, bool conflict)> done);
    void updateProjectColorAsync(const QString& id, const QString& color, qint64 version,
                                 std::function<void(bool ok, qint64 newVersion, bool conflict)> done);
    void updateProjectNameAsync(const QString& id, const QString& name, qint64 version,
                                std::function<void(bool ok, qint64 newVersion, bool conflict)> done);
    void uploadFileAsync(const QString& id, const QString& kind, const QByteArray& bytes,
                         const QString& ext, int w, int h, std::function<void(bool ok)> done);
    void downloadFileAsync(const QString& id, const QString& kind,
                           std::function<void(bool ok, QByteArray data)> done);
    void deleteProjectAsync(const QString& id, std::function<void(bool ok)> done);

    // Async version of runGuardedWrite. `attempt(version, cb)` performs one guarded PUT and
    // reports its GuardOutcome via `cb`; on a non-final Conflict, `resolve(version, cb)` re-reads
    // /merges and reports (ok, newVersion) via `cb`; the final outcome is delivered to `done`.
    // All loop state flows through the callbacks (heap-managed), so it stays static.
    static void runGuardedWriteAsync(
        int attempts, qint64 startVersion,
        std::function<void(qint64 version, std::function<void(GuardOutcome)> cb)> attempt,
        std::function<void(qint64 version, std::function<void(bool ok, qint64 newVersion)> cb)> resolve,
        std::function<void(GuardOutcome)> done);

   private:
    // Perform an HTTP request; returns the body and sets `status`. `method` is an HTTP verb;
    // `contentType` is empty for none. Synchronous (nested event loop) — used only by the retained
    // connect() handshake; every other REST call goes through requestAsync.
    QByteArray request(const QByteArray& method, const QString& path,
                       const QByteArray& body, const QString& contentType, int& status);
    // Build the authorized QNetworkRequest for `path` (shared by the sync + async paths).
    QNetworkRequest buildRequest(const QString& path, const QString& contentType) const;
    // Non-blocking request: invokes `done(status, body)` on completion (see the async
    // methods above). Sets lastError() on a transport error, like request().
    void requestAsync(const QByteArray& method, const QString& path, const QByteArray& body,
                      const QString& contentType,
                      std::function<void(int status, QByteArray body)> done);

    QNetworkAccessManager* nam_;
    QString base_;
    QString token_;
    QString err_;
    Status status_ = Status::Connecting;
  };

  // Holds the set of server connections for one window and notifies the UI when
  // it changes (so the connect dialog + projects view refresh).
  class ConnectionManager : public QObject {
    Q_OBJECT
   public:
    explicit ConnectionManager(QObject* parent = nullptr);
    ~ConnectionManager() override;

    // Connect (and add) a server. Returns true on success; sets `err` otherwise.
    bool connectTo(const QString& url, const QString& token, QString& err);
    // Disconnect a url, or (empty url) the most recently added connection.
    void disconnectFrom(const QString& url = QString());
    // Async: re-establish one connection (by url) without blocking; emits changed() and reports
    // (ok, err) via `done`. `done`'s captures must be guarded by the caller for its own lifetime.
    void reconnectAsync(const QString& url, std::function<void(bool ok, QString err)> done);
    // Async: re-establish every connection (best-effort) without blocking; emits changed() once all
    // resolve and then invokes `done`.
    void reconnectAllAsync(std::function<void()> done = {});

    QStringList urls() const;
    ServerClient* find(const QString& url) const;
    const QVector<ServerClient*>& clients() const { return clients_; }

    // Persistable view of the live set as { url, token } (see connectionStore),
    // so the connect UI can save it on every change and restore it on launch.
    QVector<SavedServer> snapshot() const;

    // Aggregate shared projects (with images) across every connection, asynchronously: fans out
    // listProjectsAsync to each client and delivers the merged set to `done` once all resolve
    // (empty when there are no connections). Non-blocking replacement for the old sync
    // sharedProjects(); callers must guard `done`'s captures for their own lifetime.
    void sharedProjectsAsync(std::function<void(QVector<ServerProject> projects)> done) const;

   signals:
    void changed();

   private:
    QVector<ServerClient*> clients_;
  };

}  // namespace stencil::net
