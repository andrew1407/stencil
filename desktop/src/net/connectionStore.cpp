#include "connectionStore.hpp"

#include <QSettings>
#include <QVariant>

namespace stencil::net {

  namespace {
    constexpr auto kServersKey = "connections/servers";
    constexpr auto kAutoConnectKey = "connections/autoConnect";
  }  // namespace

  // Stored as a flat tab-separated string per server (a QStringList): "url\ttoken",
  // or "url\ttoken\tkind" once the credential's kind is known ("admin"/"session").
  // Backwards compatible both ways — the kind is only read when the row's LAST field
  // is one of those two tags, so old two-field rows (and tokens that happen to
  // contain a tab) still load exactly as before.
  QVector<SavedServer> connectionStore::loadSavedServers() {
    QSettings s;
    const QStringList rows = s.value(kServersKey).toStringList();
    QVector<SavedServer> out;
    for (const QString& row : rows) {
      const int tab = row.indexOf('\t');
      SavedServer srv;
      srv.url = (tab < 0) ? row : row.left(tab);
      QString rest = (tab < 0) ? QString() : row.mid(tab + 1);
      const int last = rest.lastIndexOf('\t');
      if (last >= 0) {
        const QString tag = rest.mid(last + 1);
        if (tag == QLatin1String("admin") || tag == QLatin1String("session")) {
          srv.kind = tag;
          rest = rest.left(last);
        }
      }
      srv.token = rest;
      if (!srv.url.isEmpty()) out.push_back(srv);
    }
    return out;
  }

  void connectionStore::saveServers(const QVector<SavedServer>& servers) {
    QStringList rows;
    for (const SavedServer& srv : servers) {
      if (srv.url.isEmpty()) continue;
      QString row = srv.url + '\t' + srv.token;
      if (!srv.kind.isEmpty()) row += '\t' + srv.kind;  // omitted when unknown → old shape
      rows << row;
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
