#pragma once
// Desktop counterpart of browser/js/net/store.js. Backed by QSettings, except each
// token: a secret, so it lives in fileStore's owner-only (0600) secrets file.
#include <QString>
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

    // Default true.
    bool getAutoConnect();
    void setAutoConnect(bool on);
  }  // namespace connectionStore

}  // namespace stencil::net
