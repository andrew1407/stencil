// Headless check of the cross-front-end "Open in…" links: the stencil:// URL
// grammar (launchOptions parseStencilUrl), the Telegram start-payload codec, and
// the browser-fragment URL builder's percent-encoding.
//
// GOLDEN VECTORS — duplicated verbatim in browser/tests/deepLink.test.js and
// bot/tests/Stencil.TelegramBot.Tests/DeepLinkCodecTests.cs. Keep the three in sync.
#include "deepLink.hpp"
#include "launchOptions.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <cstdio>

using namespace stencil::gui;

static int failures = 0;
static void check(bool ok, const char* msg) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", msg);
  if (!ok) ++failures;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // ── parseStencilUrl grammar ──
  {
    const LaunchOptions o = parseStencilUrl(QUrl(
        "stencil://open?server=http%3A%2F%2Flocalhost%3A8090&id=p_1a2b3c_x1&version=7&incognito=1"));
    check(o.serverUrl == "http://localhost:8090", "server url decoded");
    check(o.serverProjectId == "p_1a2b3c_x1", "project id decoded");
    check(o.serverVersion == 7, "version decoded");
    check(o.incognito, "incognito flag decoded");
    check(o.src.isEmpty(), "server+id: no src");
    check(!o.empty(), "server link is not empty()");
  }
  {
    const LaunchOptions o = parseStencilUrl(QUrl(
        "stencil://open?src=https%3A%2F%2Fx.example%2Fa.png&layout=%7B%22lines%22%3A%5B%5D%7D&frame=3"));
    check(o.src == "https://x.example/a.png", "src decoded");
    check(o.layoutJson == "{\"lines\":[]}", "inline layout decoded");
    check(o.frame == 3, "frame decoded");
    check(o.serverUrl.isEmpty() && o.serverProjectId.isEmpty(), "src link: no server");
    check(!o.incognito, "incognito defaults off");
  }
  {
    // server+id beat src (the server copy is canonical).
    const LaunchOptions o = parseStencilUrl(
        QUrl("stencil://open?server=h&id=p_1&src=https%3A%2F%2Fignored.example%2Fa.png"));
    check(o.serverUrl == "h" && o.serverProjectId == "p_1", "server wins over src");
    check(o.src.isEmpty(), "src ignored when server present");
  }
  {
    const LaunchOptions bad = parseStencilUrl(QUrl("https://not-stencil.example/x"));
    check(bad.empty(), "non-stencil scheme yields empty options");
    const LaunchOptions junk = parseStencilUrl(QUrl("stencil://open?frame=zzz"));
    check(junk.empty(), "junk-only query yields empty options");
    // Links are remotely clickable — a local path must never ride the src param.
    const LaunchOptions local = parseStencilUrl(QUrl("stencil://open?src=%2Fetc%2Fpasswd"));
    check(local.src.isEmpty(), "local-path src is dropped");
    const LaunchOptions dataUrl = parseStencilUrl(
        QUrl("stencil://open?src=data%3Aimage%2Fpng%3Bbase64%2CAAA"));
    check(dataUrl.src == "data:image/png;base64,AAA", "data: src is kept");
  }

  // ── Telegram start-payload codec (shared golden vectors) ──
  {
    struct Vec { const char* url; const char* id; const char* expected; };
    const Vec vectors[] = {
        // loopback keeps http by default → scheme dropped, host|id encoded
        {"localhost:8090", "p_1a2b3c_x1", "1bG9jYWxob3N0OjgwOTB8cF8xYTJiM2NfeDE"},
        // bare remote host defaults to https → scheme dropped
        {"stencil.example.com", "p_1a2b3c_x1",
         "1c3RlbmNpbC5leGFtcGxlLmNvbXxwXzFhMmIzY194MQ"},
        // explicit http on a remote host is NOT the default → full origin kept
        {"http://stencil.example.com", "p_1", "1aHR0cDovL3N0ZW5jaWwuZXhhbXBsZS5jb218cF8x"},
        // https on a remote host IS the default → dropped, port kept
        {"https://stencil.example.com:8443", "p_1", "1c3RlbmNpbC5leGFtcGxlLmNvbTo4NDQzfHBfMQ"},
        // 47 plaintext bytes → exactly 64 payload chars (the boundary)
        {"https://hoooooooooooooooooooooooooooooooooooooooooo", "p_1",
         "1aG9vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb3xwXzE"},
    };
    for (const Vec& v : vectors) {
      const QString got = deepLink::encodeTelegramStartPayload(v.url, v.id);
      check(got == QString::fromLatin1(v.expected), v.url);
      check(got.size() <= deepLink::kTelegramStartLimit, "within the 64-char limit");
    }
    // 48 plaintext bytes → 65 payload chars → overflow → empty
    const QString host = QStringLiteral("https://h") + QString(43, QLatin1Char('o'));
    check(deepLink::encodeTelegramStartPayload(host, "p_1").isEmpty(),
          "overflow yields empty payload");
    check(deepLink::buildTelegramLink("stencil_bot", "1abc") ==
              "https://t.me/stencil_bot?start=1abc",
          "t.me link composition");
  }

  // ── browser-fragment URL builder ──
  {
    QJsonObject server;
    server["url"] = "http://localhost:8090";
    server["id"] = "p_1";
    QJsonObject payload;
    payload["server"] = server;
    payload["incognito"] = true;
    const QString url =
        deepLink::buildBrowserLaunchUrl("http://localhost:8080/", payload);
    check(url.startsWith("http://localhost:8080#stencil="),
          "base trimmed + fragment marker present");
    // The receiver does JSON.parse(decodeURIComponent(fragment)) — emulate it.
    const QString enc = url.mid(url.indexOf("#stencil=") + int(qstrlen("#stencil=")));
    const QByteArray json = QByteArray::fromPercentEncoding(enc.toLatin1());
    const QJsonObject back = QJsonDocument::fromJson(json).object();
    check(back.value("incognito").toBool(), "fragment JSON round-trips (incognito)");
    check(back.value("server").toObject().value("id").toString() == "p_1",
          "fragment JSON round-trips (server.id)");
  }

  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
