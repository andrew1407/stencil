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
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QJsonObject;

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
    qint64 updatedAt = 0;
    // Monotonic edit version (LWW guard); echoed back on PUT to detect a 409.
    qint64 version = 0;
    // Origin server (base origin) this record came from — stamped by
    // ConnectionManager::sharedProjects() so the UI can route open/save back to
    // the right connection (the desktop analogue of the browser's `serverUrl`).
    QString serverUrl;
  };

  // One connected server. REST calls are synchronous (driven by a local
  // QEventLoop) so dialogs can use them inline; each returns false and sets
  // lastError() on failure.
  class ServerClient {
   public:
    // Connection status for the UI dot: Connecting (yellow) | Connected (green) |
    // Error (red).
    enum class Status { Connecting, Connected, Error };

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
    // (POST /auth/token). Returns true when the connection is usable.
    bool connect(const QString& token = QString());

    // Re-establish a dropped connection: re-validate the current token, and (when
    // it's gone stale / been rejected) acquire a fresh one. Returns true when the
    // connection is usable again.
    bool reconnect();

    const QString& base() const { return base_; }
    const QString& token() const { return token_; }
    const QString& lastError() const { return err_; }
    Status status() const { return status_; }

    bool listProjects(QVector<ServerProject>& out);
    // Create a project; on success fills outId/outVersion. hasImage/w/h record the
    // (codec-free server's) image dimensions for the original uploaded separately.
    bool createProject(const QString& name, const QString& source, const QString& resource,
                       bool hasImage, int w, int h, QString& outId, qint64& outVersion);
    // Fetch one project in full (GET /projects/{id}): fills `meta` (name, version,
    // provenance) and the stored `layout` object (may be empty).
    bool getProject(const QString& id, ServerProject& meta, QJsonObject& layoutOut);
    // Version-guarded name/layout update (PUT /projects/{id}). On a 409 returns
    // false with `conflict` set; on success fills `newVersion`.
    bool updateProject(const QString& id, const QString& name, const QJsonObject& layout,
                       qint64 version, qint64& newVersion, bool& conflict);
    // Version-guarded color-only update (PUT /projects/{id} with just `color`).
    // `color` is "#rrggbb" or "" (clear); the server COALESCEs name/layout so they
    // stay untouched. On a 409 returns false with `conflict` set.
    bool updateProjectColor(const QString& id, const QString& color, qint64 version,
                            qint64& newVersion, bool& conflict);
    // Rename a server project (PUT {name, version}); the server COALESCEs colour/layout so they
    // stay untouched. On a 409 returns false with `conflict` set. Mirrors updateProjectColor.
    bool updateProjectName(const QString& id, const QString& name, qint64 version,
                           qint64& newVersion, bool& conflict);
    bool uploadFile(const QString& id, const QString& kind, const QByteArray& bytes,
                    const QString& ext, int w, int h);
    QByteArray downloadFile(const QString& id, const QString& kind, bool& ok);
    // Delete a project on the server (DELETE /projects/{id}). Returns false +
    // sets lastError() on failure. Used by the "move to local" flow.
    bool deleteProject(const QString& id);

   private:
    // Perform an HTTP request; returns the body and sets `status`. `method` is an
    // HTTP verb; `contentType` is empty for none.
    QByteArray request(const QByteArray& method, const QString& path,
                       const QByteArray& body, const QString& contentType, int& status);

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
    // Re-establish one connection (by url); returns false + sets `err` on failure.
    bool reconnect(const QString& url, QString& err);
    // Re-establish every connection (best-effort); always emits changed().
    void reconnectAll();

    QStringList urls() const;
    ServerClient* find(const QString& url) const;
    const QVector<ServerClient*>& clients() const { return clients_; }

    // Persistable view of the live set as { url, token } (see connectionStore),
    // so the connect UI can save it on every change and restore it on launch.
    QVector<SavedServer> snapshot() const;

    // Aggregate shared projects (with images) across every connection.
    QVector<ServerProject> sharedProjects() const;

   signals:
    void changed();

   private:
    QVector<ServerClient*> clients_;
  };

}  // namespace stencil::net
