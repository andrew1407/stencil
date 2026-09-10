#pragma once
// Persisted server connections + the auto-connect preference — the desktop
// counterpart of browser/js/net/connectionStore.js (which uses localStorage).
// Backed by QSettings (org/app set in main()), so the connected server set and
// the "auto-connect on open" toggle survive across launches — except each token,
// which is a secret and lives in fileStore's owner-only secrets file. Kept out of
// fileStore's Settings struct because the connect UI persists these on every
// change (not via the Settings dialog).
#include <QString>
#include <QVector>

namespace stencil::net {

  // One persisted connection: the server origin + the token last issued/accepted
  // for it (empty when none). Mirrors connectionStore.js's { url, token, kind }.
  struct SavedServer {
    QString url;
    QString token;
    // ServerClient::kindTag of the credential: "admin" (proven able to mint session
    // tokens), "session", or "" (unknown / none — what pre-kind rows restore as).
    QString kind;
  };

  namespace connectionStore {
    // The live server set, restored on launch (best-effort: a dead server just
    // stays absent). Returns an empty list when nothing is saved.
    QVector<SavedServer> loadSavedServers();
    // Persist the live server set (replaces the stored one).
    void saveServers(const QVector<SavedServer>& servers);

    // "Auto-connect to servers on open" preference (default true), reconnecting
    // the saved set at startup when on.
    bool getAutoConnect();
    void setAutoConnect(bool on);
  }  // namespace connectionStore

}  // namespace stencil::net
