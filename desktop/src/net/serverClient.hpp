#pragma once
// Mirrors server/internal/protocol over REST using QNetworkAccessManager. The
// desktop deliberately uses Qt Network (already linked) rather than a WebSocket
// library; live editing uses a raw QTcpSocket NDJSON transport (see the server's
// TCP listener) so no third-party dependency is added. This header covers the
// REST surface (connect/list/create/upload/download) plus a small manager that
// holds multiple connections for one window.
#include "connectionStore.hpp"
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
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
    // Per-project free-text description ("" = none). Mirrors protocol.ProjectRecord.Description.
    QString description;
    // Search keywords. Mirrors protocol.ProjectRecord.Keywords.
    QStringList keywords;
    // Blank-image fill colour ("#rrggbb" or empty = ordinary image). Mirrors
    // protocol.ProjectRecord.BlankColor; a non-empty value marks a recolourable blank project.
    QString blankColor;
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

  // One connected server. The REST surface is ASYNCHRONOUS throughout (non-blocking, driven
  // by QNetworkAccessManager): each *Async method kicks off the request and invokes its
  // completion on the GUI thread, so a slow/hostile server never freezes the UI and no nested
  // event loop re-enters paint or input. On-failure completions set lastError().
  class ServerClient {
   public:
    // Connection status for the UI dot: Connecting (yellow) | Connected (green) |
    // Expired (amber) | Error (red).
    //
    // Expired is deliberately NOT Error: the credential was refused (401/403), the
    // server itself is fine, and the saved connection (URL + label) is kept so the
    // user can sign in again from the row. Retrying it in a loop would only burn
    // requests against a token the server has already rejected.
    enum class Status { Connecting, Connected, Expired, Error };

    // Outcome of one guarded PUT (and of the guarded-write loop as a whole): the write
    // committed, hit a stale-version 409 (Conflict), or hard-failed for another reason.
    enum class GuardOutcome { Committed, Conflict, Failed };

    // What the stored credential IS (browser parity: ServerConnection.credentialKind):
    //   Admin   — PROVEN able to mint a session token: the /projects probe failed (or
    //             was skipped for a known admin credential) and minting WITH it worked,
    //             at connect OR mid-session. Only these can mint invites.
    //   Session — the supplied token passed the /projects probe directly.
    //   None    — no credential at all: the session was minted anonymously.
    enum class CredentialKind { None, Session, Admin };

    explicit ServerClient(const QString& url);
    ~ServerClient();

    // Normalize 'host:8090' / 'http://host:8090/' to a clean origin. Secure by default:
    // a bare host (no scheme) gets https, EXCEPT loopback hosts, which keep http (dev
    // servers run plaintext on localhost and the traffic never leaves the machine).
    // The saved credential was refused — re-authenticate, never retry blindly.
    bool needsReauth() const { return status_ == Status::Expired; }

    static QString normalizeBase(const QString& raw);
    // Invite links, "<url>#token=<tok>": split one — returns the URL sans fragment
    // and sets `token` to the fragment's value ("" when there is none). Call it
    // BEFORE normalizeBase, which silently drops any fragment.
    static QString splitInviteToken(const QString& raw, QString& token);
    static QString inviteLink(const QString& base, const QString& token);
    // True for a loopback/localhost host (127.0.0.0/8, ::1, "localhost", "*.localhost"),
    // where plaintext http is safe because the bytes never hit the network.
    static bool isLoopbackHost(const QString& host);
    // True when `base` would send the bearer token + image bytes in CLEARTEXT to a remote
    // host (scheme http and not loopback) — the UI warns on these.
    static bool isInsecureRemote(const QString& base);

    const QString& base() const { return base_; }
    const QString& token() const { return token_; }
    // What the user supplied at connect: outlives server restarts (a minted
    // session token dies with them), so it is what snapshot() persists.
    const QString& credential() const { return credential_; }
    const QString& lastError() const { return err_; }
    Status status() const { return status_; }
    // …and what that credential turned out to be (persisted alongside it).
    CredentialKind credentialKind() const { return kind_; }
    bool isAdmin() const { return kind_ == CredentialKind::Admin; }

    // Persistence tags for CredentialKind — "admin" / "session" / "" (None).
    static QString kindTag(CredentialKind k);
    static CredentialKind kindFromTag(const QString& tag);

    // Each kicks off the request and invokes `done` on the GUI thread when the reply
    // completes; error strings and 409→conflict semantics match the REST wire contract.
    // Callers must guard the callback's captures (QPointer) so a reply finishing after the
    // caller is destroyed is a safe no-op; one finishing after THIS client is destroyed is
    // already safe (the connection is bound to nam_, which dies with the client).
    void connectAsync(const QString& token, std::function<void(bool ok)> done,
                      CredentialKind hint = CredentialKind::None);
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
    // Update just the description (empty string clears it), mirroring updateProjectColorAsync.
    // Defined inline (header-only) so it reuses the private putGuarded without a matching .cpp edit.
    void updateProjectDescriptionAsync(const QString& id, const QString& description, qint64 version,
                                       std::function<void(bool ok, qint64 newVersion, bool conflict)> done) {
      QJsonObject obj;
      obj.insert("description", description);  // always sent (even "") so a clear reaches the server
      putGuarded(id, obj, version, "update", done);
    }
    // Update just the search keywords (an empty list clears them), mirroring
    // updateProjectDescriptionAsync — protocol.ProjectUpdate.Keywords ([] = clear).
    void updateProjectKeywordsAsync(const QString& id, const QStringList& keywords, qint64 version,
                                    std::function<void(bool ok, qint64 newVersion, bool conflict)> done) {
      QJsonObject obj;
      obj.insert("keywords", QJsonArray::fromStringList(keywords));  // always sent, so [] reaches the server
      putGuarded(id, obj, version, "update", done);
    }
    void updateProjectNameAsync(const QString& id, const QString& name, qint64 version,
                                std::function<void(bool ok, qint64 newVersion, bool conflict)> done);
    void uploadFileAsync(const QString& id, const QString& kind, const QByteArray& bytes,
                         const QString& ext, int w, int h, std::function<void(bool ok)> done);
    void downloadFileAsync(const QString& id, const QString& kind,
                           std::function<void(bool ok, QByteArray data)> done);
    // Per-file delete for filestore-only kinds (video/variantN/chat) — the
    // server's idempotent DELETE route (llm-contract.md §9).
    void deleteFileAsync(const QString& id, const QString& kind,
                         std::function<void(bool ok)> done);
    void deleteProjectAsync(const QString& id, std::function<void(bool ok)> done);
    // Mint a FRESH session with the stored credential (bearer, label "invite") and
    // deliver the link "<base>#token=<fresh>". The live session token is untouched;
    // fails at once when no credential is held (anonymous sessions can't invite).
    void mintInviteAsync(std::function<void(bool ok, QString link)> done);

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
    // Build the authorized QNetworkRequest for `path` (shared by the sync + async paths).
    // `bearer` overrides the session token (used by the invite mint); empty = token_.
    QNetworkRequest buildRequest(const QString& path, const QString& contentType,
                                 const QString& bearer = QString()) const;
    // Non-blocking request: invokes `done(status, body)` on completion (see the async
    // methods above). Sets lastError() on a transport error, like request(). `retried`
    // marks the one credential re-mint retry, so a refusal never mints twice.
    void requestAsync(const QByteArray& method, const QString& path, const QByteArray& body,
                      const QString& contentType,
                      std::function<void(int status, QByteArray body)> done,
                      bool retried = false);
    // Shared body for the three guarded PUT variants (layout / colour / name). `obj` is the
    // request body sans version; `verb` names the op for the error string ("update"/"rename").
    // Reports 409 as the third `done` arg (conflict) so the caller can prompt a reload.
    void putGuarded(const QString& id, QJsonObject obj, qint64 version, const char* verb,
                    std::function<void(bool, qint64, bool)> done);

    QNetworkAccessManager* nam_;
    QString base_;
    QString token_;
    QString credential_;
    CredentialKind kind_ = CredentialKind::None;
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

    // Connect (and add) a server, reporting (ok, err) when the handshake resolves.
    // `kindHint` carries a previously proven CredentialKind (from the saved set).
    void connectToAsync(const QString& url, const QString& token,
                        std::function<void(bool ok, QString err)> done,
                        ServerClient::CredentialKind kindHint = ServerClient::CredentialKind::None);
    // Disconnect a url, or (empty url) the most recently added connection.
    void disconnectFrom(const QString& url = QString());
    // Reorder the live connection set: move the client at index `from` to index `to`
    // (QList::move semantics). Emits changed() so the new order is persisted (via the
    // window's changed()→saveServers hook) and the UI refreshes — mirrors the browser
    // ConnectionManager.reorder().
    void reorder(int from, int to);
    // Re-establish one connection (by url); emits changed() and reports (ok, err) via `done`,
    // whose captures the caller must guard for its own lifetime.
    void reconnectAsync(const QString& url, std::function<void(bool ok, QString err)> done);
    // Sign an EXISTING connection in again with a freshly supplied credential — the
    // expired row's token prompt. connectToAsync() cannot do this: a refused client keeps
    // its place on purpose, so that path trips its own "already connected" guard. Falls
    // back to connectToAsync when the url is not listed at all.
    void reauthenticateAsync(const QString& url, const QString& token,
                             std::function<void(bool ok, QString err)> done);
    // Re-establish every connection (best-effort); emits changed() once all resolve, then
    // invokes `done`.
    void reconnectAllAsync(std::function<void()> done = {});

    QStringList urls() const;
    ServerClient* find(const QString& url) const;
    const QVector<ServerClient*>& clients() const { return clients_; }

    // Persistable view of the live set as { url, token } (see connectionStore),
    // so the connect UI can save it on every change and restore it on launch.
    QVector<SavedServer> snapshot() const;

    // Aggregate shared projects (with images) across every connection, asynchronously: fans out
    // listProjectsAsync to each client and delivers the merged set to `done` once all resolve
    // (empty when there are no connections). Callers must guard `done`'s captures for
    // their own lifetime.
    void sharedProjectsAsync(std::function<void(QVector<ServerProject> projects)> done) const;

   signals:
    void changed();

   private:
    QVector<ServerClient*> clients_;
    // Clients still shaking hands: held so this manager's destruction takes their network
    // access managers with them, which severs the in-flight reply's callback.
    QVector<ServerClient*> pending_;
  };

}  // namespace stencil::net
