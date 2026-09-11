#include "connectionStore.hpp"
#include "fileStore.hpp"

#include <QJsonObject>
#include <QSettings>
#include <QVariant>

namespace stencil::net {

  namespace {
    constexpr auto kServersKey = "connections/servers";
    constexpr auto kAutoConnectKey = "connections/autoConnect";
    constexpr auto kTokensKey = "serverTokens";
  }  // namespace

  // One tab-separated row per server: "url\t", or "url\t\tkind" once the credential's
  // kind is known. The middle field is a LEGACY cleartext token, migrated on load into
  // fileStore's secrets file; kind is read only when it is the row's LAST field, so
  // legacy rows (and tokens containing a tab) still load.
  QVector<SavedServer> connectionStore::loadSavedServers() {
    QSettings s;
    const QStringList rows = s.value(kServersKey).toStringList();
    const QJsonObject tokens = gui::fileStore::loadSecrets().value(kTokensKey).toObject();
    QVector<SavedServer> out;
    bool plaintext = false;
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
      srv.token = rest.isEmpty() ? tokens.value(srv.url).toString() : rest;
      plaintext = plaintext || !rest.isEmpty();
      if (!srv.url.isEmpty()) out.push_back(srv);
    }
    // One-time migration off the plaintext rows an older build wrote.
    if (plaintext) saveServers(out);
    return out;
  }

  void connectionStore::saveServers(const QVector<SavedServer>& servers) {
    QStringList rows;
    QJsonObject tokens;
    for (const SavedServer& srv : servers) {
      if (srv.url.isEmpty()) continue;
      QString row = srv.url + '\t';  // the token field stays empty — see above
      if (!srv.kind.isEmpty()) row += '\t' + srv.kind;  // omitted when unknown → old shape
      rows << row;
      if (!srv.token.isEmpty()) tokens.insert(srv.url, srv.token);
    }
    QSettings s;
    s.setValue(kServersKey, rows);
    QJsonObject secrets = gui::fileStore::loadSecrets();
    secrets.insert(kTokensKey, tokens);
    gui::fileStore::saveSecrets(secrets);
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
