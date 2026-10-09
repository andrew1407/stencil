#pragma once
// Desktop counterpart of browser/js/net/connectionStore.js. Backed by QSettings, except each
// token: a secret, so it lives in fileStore's owner-only (0600) secrets file.
#include <QString>
#include <QStringList>
#include <QVector>

namespace stencil::net {

  // Mirrors chat/store.js's { url, token, kind }.
  struct SavedServer {
    QString url;
    QString token;
    // ServerClient::kindTag: "admin", "session", or "" (what pre-kind rows restore as).
    QString kind;
  };

  namespace connectionStore {
    QVector<SavedServer> loadSavedServers();
    void saveServers(const QVector<SavedServer>& servers);
    // The live connections in their order, then every saved one neither live nor `forgotten`: a
    // server unreachable now, or held by another window, keeps its row and its token.
    QVector<SavedServer> merged(const QVector<SavedServer>& live, const QVector<SavedServer>& saved,
                                const QStringList& forgotten);
    void saveKeeping(const QVector<SavedServer>& live, const QStringList& forgotten);

    // Default true.
    bool getAutoConnect();
    void setAutoConnect(bool on);
  }  // namespace connectionStore

}  // namespace stencil::net
