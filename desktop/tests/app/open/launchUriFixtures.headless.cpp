// Walks the shared launch-URI corpus (common/fixtures/deepLink/launchQuery.json, format in its
// _schema.md) through src/app/open/launchUri.cpp: every vector's URL parts parse to its
// expectation, the fields it leaves out being the empty query's; and the desktop's own subset
// (adoptLaunchUri) lands the server, project, source and connections on LaunchOptions.
#include "launchOptions.hpp"
#include "launchUri.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <cstdio>

#include "../../support/check.hpp"
#include "../../support/fixtureCorpus.hpp"

using namespace stencil::gui;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  std::printf("deepLink/launchQuery:\n");
  bool ok = false;
  const QJsonArray cases = readJsonFile(corpusPath("deepLink/launchQuery.json"), &ok).array();
  check(ok && cases.size() >= 20, "launchQuery.json loads");
  for (const QJsonValue& cv : cases) {
    const QJsonObject c = cv.toObject();
    const QString name = c.value("name").toString();
    const auto got = launchUri::parseParts(c.value("search").toString(), c.value("hash").toString());
    const QJsonValue expect = c.value("expect");
    if (expect.isNull()) {
      check(!got.has_value(), qPrintable(name + " → null"));
      continue;
    }
    if (!got) {
      check(false, qPrintable(name + " → a query, got null"));
      continue;
    }
    // The vector's `expect` is merged over the empty query.
    QJsonObject want = launchUri::LaunchUri{}.toJson();
    const QJsonObject e = expect.toObject();
    for (auto it = e.begin(); it != e.end(); ++it) want.insert(it.key(), it.value());
    checkJsonEq(got->toJson(), want, name);
  }
  std::printf("  walked %d vectors\n", int(cases.size()));

  // A whole URL splits into the same parts, and a stencil:// link reads the same way.
  {
    const auto web = launchUri::parse(QUrl("https://app.example/?theme=dark#connect=https://srv.example&token=t"));
    check(web && web->theme == "dark" && web->connect.size() == 1 && web->connect[0].token == "t", "a whole URL: query and fragment");
    const auto scheme = launchUri::parse(QUrl("stencil://open?src=https%3A%2F%2Fx%2Fa.png&frame=3"));
    const auto* source = scheme && scheme->picture ? std::get_if<launchUri::SourcePicture>(&*scheme->picture) : nullptr;
    check(source && source->url == "https://x/a.png" && source->frame == 3.0, "a stencil:// link");
    check(!launchUri::parse(QUrl("file:///tmp/x?source=y")), "another scheme is no link");
    const auto picture = launchUri::parse(QUrl("https://cdn.example/a.png?width=800"));
    check(!picture || !picture->actionable(), "a picture URL is not actionable");
  }

  // The desktop's subset onto LaunchOptions.
  {
    LaunchOptions o;
    adoptLaunchUri(o, *launchUri::parse(QUrl("https://app.example/?project=localhost:8080/projects/p_1&theme=auto")));
    check(o.serverUrl == "localhost:8080" && o.serverProjectId == "p_1", "a project locator is a server reference");
    check(o.hasTheme && o.theme == "system", "theme=auto is the system mode");
    LaunchOptions bare;
    adoptLaunchUri(bare, *launchUri::parse(QUrl("https://app.example/?project=Sketch")));
    check(bare.project == "Sketch" && bare.serverUrl.isEmpty(), "a bare ref with no server is a local project");
    LaunchOptions viaConnect;
    adoptLaunchUri(viaConnect, *launchUri::parse(QUrl("https://app.example/#project=p_2&connect=http://h:8090&token=tok")));
    check(viaConnect.serverUrl == "http://h:8090" && viaConnect.serverProjectId == "p_2", "a bare ref takes the launch's server");
    check(viaConnect.connect.size() == 1 && viaConnect.connect[0].token == "tok", "the connection and its fragment token ride along");
    LaunchOptions src;
    adoptLaunchUri(src, *launchUri::parse(QUrl("https://app.example/?source=https://x/clip.mp4&frame=12&layout=https://x/l.json&incognito")));
    check(src.src == "https://x/clip.mp4" && src.frame == 12 && src.layout == "https://x/l.json" && src.incognito, "source, frame, layout url and incognito");
    LaunchOptions inlineLayout;
    adoptLaunchUri(inlineLayout, *launchUri::parse(QUrl("https://app.example/?blank=red&layout-raw=%7B%22lines%22%3A%5B%5D%7D")));
    check(inlineLayout.layoutJson == "{\"lines\":[]}" && inlineLayout.src.isEmpty(), "inline layout rides as JSON; a blank has no flag here");
  }

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILURE" : "SUCCESS", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
