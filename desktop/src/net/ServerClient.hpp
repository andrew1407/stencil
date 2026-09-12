#pragma once
// REST mirror of server/internal/protocol; live edits ride a raw QTcpSocket NDJSON transport (no WebSocket dependency).
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

  struct ServerProject {
    QString id;
    QString name;
    // Mirrors protocol.ProjectRecord.Color; empty = theme default.
    QString color;
    QString description;
    QStringList keywords;
    // protocol.ProjectRecord.BlankColor; non-empty marks a recolourable blank project.
    QString blankColor;
    bool hasImage = false;
    int imageW = 0;
    int imageH = 0;
    QString source;
    QString resource;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    // Epoch ms; 0 = never (protocol.ProjectRecord.ExpiresAt).
    qint64 expiresAt = 0;
    // Monotonic edit version (LWW guard); echoed back on PUT to detect a 409.
    qint64 version = 0;
    // Stamped by ConnectionManager::sharedProjects() so open/save route back to the right connection.
    QString serverUrl;
  };

  // The REST surface is asynchronous throughout: completions run on the GUI thread, never in a nested loop.
  class ServerClient {
   public:
    // Expired (401/403) is deliberately not Error: the server is fine and the saved row is kept for re-sign-in.
    enum class Status { CONNECTING, CONNECTED, EXPIRED, ERROR };

    enum class GuardOutcome { COMMITTED, CONFLICT, FAILED };

    // Browser parity: ServerConnection.credentialKind. Admin has proven it can mint a session token.
    enum class CredentialKind { NONE, SESSION, ADMIN };

    explicit ServerClient(const QString& url);
    ~ServerClient();

    bool needsReauth() const { return status_ == Status::EXPIRED; }

    static QString normalizeBase(const QString& raw);
    // Split "<url>#token=<tok>" BEFORE normalizeBase, which drops the fragment.
    static QString splitInviteToken(const QString& raw, QString& token);
    static QString inviteLink(const QString& base, const QString& token);
    // Loopback (127/8, ::1, *.localhost) keeps plaintext http — the bytes never hit the network; a bare remote host gets https.
    static bool isLoopbackHost(const QString& host);
    // http to a non-loopback host sends the bearer token in cleartext — the UI warns.
    static bool isInsecureRemote(const QString& base);

    const QString& base() const { return base_; }
    const QString& token() const { return token_; }
    // Outlives server restarts (a minted session token does not), so snapshot() persists it.
    const QString& credential() const { return credential_; }
    const QString& lastError() const { return err_; }
    Status status() const { return status_; }
    CredentialKind credentialKind() const { return kind_; }
    bool isAdmin() const { return kind_ == CredentialKind::ADMIN; }

    static QString kindTag(CredentialKind k);
    static CredentialKind kindFromTag(const QString& tag);

    // Completions run on the GUI thread; callers guard captures with QPointer. After the client
    // dies the reply is a no-op — the connection is bound to nam_.
    void connectAsync(const QString& token, std::function<void(bool ok)> done,
                      CredentialKind hint = CredentialKind::NONE);
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
    void updateProjectDescriptionAsync(const QString& id, const QString& description, qint64 version,
                                       std::function<void(bool ok, qint64 newVersion, bool conflict)> done) {
      QJsonObject obj;
      obj.insert("description", description);
      putGuarded(id, obj, version, "update", done);
    }
    void updateProjectKeywordsAsync(const QString& id, const QStringList& keywords, qint64 version,
                                    std::function<void(bool ok, qint64 newVersion, bool conflict)> done) {
      QJsonObject obj;
      obj.insert("keywords", QJsonArray::fromStringList(keywords));
      putGuarded(id, obj, version, "update", done);
    }
    void updateProjectNameAsync(const QString& id, const QString& name, qint64 version,
                                std::function<void(bool ok, qint64 newVersion, bool conflict)> done);
    void uploadFileAsync(const QString& id, const QString& kind, const QByteArray& bytes,
                         const QString& ext, int w, int h, std::function<void(bool ok)> done);
    void downloadFileAsync(const QString& id, const QString& kind,
                           std::function<void(bool ok, QByteArray data)> done);
    // Filestore-only kinds (video/variantN/chat) — the idempotent DELETE route (llm-contract.md §9).
    void deleteFileAsync(const QString& id, const QString& kind,
                         std::function<void(bool ok)> done);
    void deleteProjectAsync(const QString& id, std::function<void(bool ok)> done);
    // A fresh session minted with the credential; the live token is untouched.
    void mintInviteAsync(std::function<void(bool ok, QString link)> done);

    // Guarded-write loop: `attempt` does one PUT; on a non-final Conflict `resolve` re-reads and merges.
    static void runGuardedWriteAsync(
        int attempts, qint64 startVersion,
        std::function<void(qint64 version, std::function<void(GuardOutcome)> cb)> attempt,
        std::function<void(qint64 version, std::function<void(bool ok, qint64 newVersion)> cb)> resolve,
        std::function<void(GuardOutcome)> done);

   private:
    // `bearer` overrides the session token (invite mint); empty = token_.
    QNetworkRequest buildRequest(const QString& path, const QString& contentType,
                                 const QString& bearer = QString()) const;
    // `retried` marks the one credential re-mint retry, so a refusal never mints twice.
    void requestAsync(const QByteArray& method, const QString& path, const QByteArray& body,
                      const QString& contentType,
                      std::function<void(int status, QByteArray body)> done,
                      bool retried = false);
    // A 409 arrives as the third `done` arg (conflict).
    void putGuarded(const QString& id, QJsonObject obj, qint64 version, const char* verb,
                    std::function<void(bool, qint64, bool)> done);

    QNetworkAccessManager* nam_;
    QString base_;
    QString token_;
    QString credential_;
    CredentialKind kind_ = CredentialKind::NONE;
    QString err_;
    Status status_ = Status::CONNECTING;
  };

  class ConnectionManager : public QObject {
    Q_OBJECT
   public:
    explicit ConnectionManager(QObject* parent = nullptr);
    ~ConnectionManager() override;

    // `kindHint` is a previously proven CredentialKind from the saved set.
    void connectToAsync(const QString& url, const QString& token,
                        std::function<void(bool ok, QString err)> done,
                        ServerClient::CredentialKind kindHint = ServerClient::CredentialKind::NONE);
    void disconnectFrom(const QString& url = QString());
    // QList::move semantics; emits changed() so the order persists. Browser: ConnectionManager.reorder().
    void reorder(int from, int to);
    // Callers guard `done`'s captures for their own lifetime.
    void reconnectAsync(const QString& url, std::function<void(bool ok, QString err)> done);
    // connectToAsync() cannot re-sign an expired row: a refused client keeps its place and trips
    // the "already connected" guard. Falls back to connectToAsync for an unlisted url.
    void reauthenticateAsync(const QString& url, const QString& token,
                             std::function<void(bool ok, QString err)> done);
    void reconnectAllAsync(std::function<void()> done = {});

    QStringList urls() const;
    ServerClient* find(const QString& url) const;
    const QVector<ServerClient*>& clients() const { return clients_; }

    QVector<SavedServer> snapshot() const;

    // Fans out listProjectsAsync to every client; callers guard `done`'s captures.
    void sharedProjectsAsync(std::function<void(QVector<ServerProject> projects)> done) const;

   signals:
    void changed();

   private:
    QVector<ServerClient*> clients_;
    // Held so this manager's destruction takes their nam_ with them, severing the in-flight callback.
    QVector<ServerClient*> pending_;
  };

}  // namespace stencil::net
