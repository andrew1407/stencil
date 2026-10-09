#include "launchOptions.hpp"
#include "webScheme.hpp"
#include "deepLink.hpp"
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

namespace stencil::gui {

  LaunchOptions parseLaunchOptions(const QCoreApplication& app) {
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Stencil — image annotation / drawing tool");
    parser.addHelpOption();

    // Names are matched exactly, so --project (value) and --projects (flag) never collide.
    const QCommandLineOption themeOpt(
        "theme", "Set the default theme: dark or light.", "dark|light");
    const QCommandLineOption projectOpt(
        "project", "Open an existing saved project by name.", "name");
    const QCommandLineOption srcOpt(
        "src", "Open an image by path or URL, or a video file/URL.", "path|url");
    const QCommandLineOption frameOpt(
        "frame", "Video frame to open (0-based; default first frame).", "n", "0");
    const QCommandLineOption incognitoOpt(
        "incognito", "Edit without saving (only with an image --src).");
    const QCommandLineOption layoutOpt(
        "layout", "Apply a layout JSON (path or URL) after --src loads.",
        "path|url");
    const QCommandLineOption projectsOpt(
        "projects", "Open the Projects window at launch.");
    parser.addOptions({themeOpt, projectOpt, srcOpt, frameOpt, incognitoOpt,
                       layoutOpt, projectsOpt});
    // A bare file path ("Open With"), opened via the drag-and-drop path.
    parser.addPositionalArgument("file", "Image, video, or layout JSON to open.",
                                 "[file]");

    // process() honours --help/--version and exits on a malformed command line.
    parser.process(app);

    LaunchOptions o;
    if (parser.isSet(themeOpt)) {
      const QString t = parser.value(themeOpt).trimmed().toLower();
      // Anything but the two modes is ignored, so a typo never clobbers the saved preference.
      if (t == "dark" || t == "light") {
        o.hasTheme = true;
        o.theme = t;
      }
    }
    o.project = parser.value(projectOpt).trimmed();
    o.src = parser.value(srcOpt).trimmed();
    if (parser.isSet(frameOpt)) {
      bool ok = false;
      const int n = parser.value(frameOpt).toInt(&ok);
      o.frame = (ok && n > 0) ? n : 0;  // invalid/negative -> first frame
    }
    o.incognito = parser.isSet(incognitoOpt);
    o.layout = parser.value(layoutOpt).trimmed();
    o.projects = parser.isSet(projectsOpt);
    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
      const QString p = positional.first().trimmed();
      // A stencil:// deep link (Linux scheme handlers pass the URL as argv %u); other flags still apply.
      if (p.startsWith(QLatin1String("stencil:"), Qt::CaseInsensitive)) {
        adoptLinkOptions(o, parseStencilUrl(QUrl(p)));
      } else {
        o.file = p;
      }
    }
    return o;
  }

  void adoptLinkOptions(LaunchOptions& o, const LaunchOptions& link) {
    o.serverUrl = link.serverUrl;
    o.serverProjectId = link.serverProjectId;
    if (!link.src.isEmpty()) o.src = link.src;
    if (!link.layoutJson.isEmpty()) o.layoutJson = link.layoutJson;
    if (link.frame > 0) o.frame = link.frame;
    o.incognito = o.incognito || link.incognito;
    o.script = link.script;
    o.scriptDropped = link.scriptDropped;
  }

  LaunchOptions parseStencilUrl(const QUrl& url) {
    LaunchOptions o;
    if (url.scheme().compare(QLatin1String("stencil"), Qt::CaseInsensitive) != 0)
      return o;
    const QUrlQuery q(url);
    const QString server = q.queryItemValue("server", QUrl::FullyDecoded).trimmed();
    const QString id = q.queryItemValue("id", QUrl::FullyDecoded).trimmed();
    if (!server.isEmpty() && !id.isEmpty()) {
      // A server reference wins over inline content (the server copy is canonical).
      o.serverUrl = server;
      o.serverProjectId = id;
    } else {
      o.src = q.queryItemValue("src", QUrl::FullyDecoded).trimmed();
      // Deep links are remotely clickable: only web/data image sources may ride them, never LOCAL files.
      if (!o.src.isEmpty() && !net::fetchGuard::isWebScheme(o.src)
          && !o.src.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
        o.src.clear();
      }
      // Bounded: parsed as JSON downstream, so the same cap as the browser hand-off payload.
      const QString layout = q.queryItemValue("layout", QUrl::FullyDecoded).trimmed();
      if (layout.size() <= deepLink::browserLaunchPayloadMax()) o.layoutJson = layout;
      bool ok = false;
      const int n = q.queryItemValue("frame").toInt(&ok);
      o.frame = (ok && n > 0) ? n : 0;
    }
    o.incognito = q.queryItemValue("incognito") == QLatin1String("1");
    const QString script = q.queryItemValue("script", QUrl::FullyDecoded);
    if (script.size() > launchScriptMaxChars()) o.scriptDropped = true;
    else if (!script.isEmpty()) o.script = script;
    return o;
  }

  int launchScriptMaxChars() {
    static const int cap = [] {
      QFile f(QStringLiteral(":/config/constants.json"));
      const int fallback = 200000;   // a build without the qrc
      if (!f.open(QIODevice::ReadOnly)) return fallback;
      const int v = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("LAUNCH"))
                        .toObject().value(QLatin1String("scriptMaxChars")).toInt(0);
      return v > 0 ? v : fallback;
    }();
    return cap;
  }

}
