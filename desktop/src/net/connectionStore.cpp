#include "connectionStore.hpp"

#include <QSettings>
#include <QVariant>

namespace stencil::net {

  namespace {
    constexpr auto kServersKey = "connections/servers";
    constexpr auto kAutoConnectKey = "connections/autoConnect";
  }  // namespace

  // Stored as a flat "url\ttoken" string per server (a QStringList), so the token
  // (which may be empty) round-trips without a nested structure.
  QVector<SavedServer> connectionStore::loadSavedServers() {
    QSettings s;
    const QStringList rows = s.value(kServersKey).toStringList();
    QVector<SavedServer> out;
    for (const QString& row : rows) {
      const int tab = row.indexOf('\t');
      SavedServer srv;
      srv.url = (tab < 0) ? row : row.left(tab);
      srv.token = (tab < 0) ? QString() : row.mid(tab + 1);
      if (!srv.url.isEmpty()) out.push_back(srv);
    }
    return out;
  }

  void connectionStore::saveServers(const QVector<SavedServer>& servers) {
    QStringList rows;
    for (const SavedServer& srv : servers) {
      if (srv.url.isEmpty()) continue;
      rows << (srv.url + '\t' + srv.token);
    }
    QSettings s;
    s.setValue(kServersKey, rows);
  }

  bool connectionStore::getAutoConnect() {
    QSettings s;
    return s.value(kAutoConnectKey, true).toBool();  // default on
  }

  void connectionStore::setAutoConnect(bool on) {
    QSettings s;
    s.setValue(kAutoConnectKey, on);
  }

}  // namespace stencil::net
