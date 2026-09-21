// Headless check that a saved connection's TOKEN is a secret at rest (net/connectionStore
// + io/fileStore): the url/kind list still lives in QSettings, but the token itself only
// ever lands in the owner-only (0600) secrets file, a row written by an older build is
// migrated out of QSettings on first load, and every shape still loads exactly as before.
// Pure QtCore + the isolated state dir ctest hands it — no server, no display.
#include "connectionStore.hpp"
#include "fileStore.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QStringList>
#include <cstdio>

#include "../../support/check.hpp"

using stencil::net::SavedServer;
namespace store = stencil::net::connectionStore;

namespace {

  const auto ROWS_KEY = QStringLiteral("connections/servers");

  // What QSettings holds right now, as one blob to search for a leaked token.
  QString settingsRows() {
    QSettings s;
    return s.value(ROWS_KEY).toStringList().join(QLatin1Char('\n'));
  }

  SavedServer server(const char* url, const char* token, const char* kind) {
    SavedServer s;
    s.url = QString::fromLatin1(url);
    s.token = QString::fromLatin1(token);
    s.kind = QString::fromLatin1(kind);
    return s;
  }

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  // Keep these reads/writes out of the real per-user config (the state dir is isolated
  // by ctest; QSettings needs its own org/app to be).
  QCoreApplication::setOrganizationName("StencilTest");
  QCoreApplication::setApplicationName("connectionSecretsHeadless");
  QSettings().clear();
  QFile::remove(stencil::gui::fileStore::secretsPath());

  std::printf("tokens at rest:\n");
  {
    store::saveServers({server("http://secret.example:8090", "s3cr3t-token", "admin"),
                        server("http://other.example:8091", "second-token", "session")});
    check(!settingsRows().contains(QStringLiteral("s3cr3t-token")) &&
              !settingsRows().contains(QStringLiteral("second-token")),
          "no saved token reaches QSettings");
    check(settingsRows().contains(QStringLiteral("http://secret.example:8090")),
          "…while the url list stays where it was");
    const auto back = store::loadSavedServers();
    check(back.size() == 2 && back[0].url == QStringLiteral("http://secret.example:8090") &&
              back[0].token == QStringLiteral("s3cr3t-token") &&
              back[0].kind == QStringLiteral("admin"),
          "…and the token round-trips out of the protected store, kind and all");
    check(back.size() == 2 && back[1].token == QStringLiteral("second-token") &&
              back[1].kind == QStringLiteral("session"),
          "…for every saved server, in order");
  }
#ifdef Q_OS_UNIX
  check(!QFile(stencil::gui::fileStore::secretsPath())
             .permissions()
             .testAnyFlags(QFileDevice::ReadGroup | QFileDevice::ReadOther |
                           QFileDevice::WriteGroup | QFileDevice::WriteOther),
        "the secrets file is readable only by its owner");
#endif

  std::printf("migration off the plaintext rows:\n");
  {
    // What an older build wrote: "url\ttoken\tkind" (and the pre-kind "url\ttoken").
    QSettings s;
    s.setValue(ROWS_KEY, QStringList{QStringLiteral("http://old.example:8090\tplain-tok\tsession"),
                                     QStringLiteral("http://older.example:8090\tolder-tok")});
    const auto migrated = store::loadSavedServers();
    check(migrated.size() == 2 && migrated[0].token == QStringLiteral("plain-tok") &&
              migrated[0].kind == QStringLiteral("session") &&
              migrated[1].token == QStringLiteral("older-tok") && migrated[1].kind.isEmpty(),
          "a plaintext row loads unchanged, kind or none");
    check(!settingsRows().contains(QStringLiteral("plain-tok")) &&
              !settingsRows().contains(QStringLiteral("older-tok")),
          "…and both tokens are gone from QSettings right afterwards");
    const auto reloaded = store::loadSavedServers();
    check(reloaded.size() == 2 && reloaded[0].token == QStringLiteral("plain-tok") &&
              reloaded[0].kind == QStringLiteral("session") &&
              reloaded[1].token == QStringLiteral("older-tok"),
          "…while still loading identically from the protected store");
  }

  std::printf("shapes that must not change:\n");
  {
    QSettings s;
    // A token containing a tab still survives: only a RECOGNISED trailing tag is a kind.
    s.setValue(ROWS_KEY, QStringList{QStringLiteral("http://tab.example:8090\tto\tken")});
    const auto tabbed = store::loadSavedServers();
    check(tabbed.size() == 1 && tabbed[0].token == QStringLiteral("to\tken") &&
              tabbed[0].kind.isEmpty(),
          "a trailing field that is not a kind tag stays part of the token");
    // A server with no token at all round-trips as one.
    store::saveServers({server("http://none.example:8090", "", "")});
    const auto none = store::loadSavedServers();
    check(none.size() == 1 && none[0].url == QStringLiteral("http://none.example:8090") &&
              none[0].token.isEmpty() && none[0].kind.isEmpty(),
          "…and a connection with no token stays tokenless");
    // An empty url is dropped on save, exactly as before.
    store::saveServers({server("", "orphan-token", "admin")});
    check(store::loadSavedServers().isEmpty() &&
              !settingsRows().contains(QStringLiteral("orphan-token")),
          "…while a row with no url is dropped, token and all");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
