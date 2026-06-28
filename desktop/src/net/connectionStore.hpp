#pragma once
// Persisted server connections + the auto-connect preference — the desktop
// counterpart of browser/js/net/connectionStore.js (which uses localStorage).
// Backed by QSettings (org/app set in main()), so the connected server set and
// the "auto-connect on open" toggle survive across launches. Kept out of
// fileStore's Settings struct because the tokens are connection secrets, not UI
// settings, and the connect UI persists them on every change (not via the
// Settings dialog).
#include <QString>
#include <QVector>

namespace stencil::net {

  // One persisted connection: the server origin + the token last issued/accepted
  // for it (empty when none). Mirrors connectionStore.js's { url, token } shape.
  struct SavedServer {
    QString url;
    QString token;
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
