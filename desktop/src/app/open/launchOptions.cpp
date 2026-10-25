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
#include <algorithm>

namespace stencil::gui {

  namespace {
    // An http(s) positional is a launch URI when it carries something to act on; else a file to open.
    LaunchOptions fromWebLink(const QString& spec) {
      LaunchOptions o;
      if (!net::fetchGuard::isWebScheme(spec)) return o;
      const auto link = launchUri::parse(QUrl(spec));
      if (link && link->actionable()) adoptLaunchUri(o, *link);
      return o;
    }

    // Each picture kind onto the flag it stands for; a blank has none here.
    struct PictureAdopter {
      LaunchOptions& o;
      const launchUri::LaunchUri& link;
      void operator()(const launchUri::ServerPicture& p) const {
        o.serverUrl = p.url;
        o.serverProjectId = p.id;
        o.serverVersion = p.version > 0 ? qint64(p.version) : 0;
      }
      void operator()(const launchUri::ProjectPicture& p) const {
        // A bare ref with no server anywhere in the link is a saved local project by name.
        if (p.server) o.serverUrl = *p.server;
        else if (!link.connect.empty()) o.serverUrl = link.connect.front().url;
        if (o.serverUrl.isEmpty()) o.project = p.ref;
        else o.serverProjectId = p.ref;
      }
      void operator()(const launchUri::SourcePicture& p) const {
        o.src = p.url;
        o.srcFromLink = true;
        o.frame = (p.frame && *p.frame > 0) ? int(*p.frame) : 0;
      }
      void operator()(const launchUri::BlankPicture&) const {}
    };
  }  // namespace

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
    // A bare file path ("Open With"), a stencil:// link, or the browser app's launch URL.
    parser.addPositionalArgument("file", "Image, video, layout JSON or launch link to open.",
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
      } else if (const LaunchOptions web = fromWebLink(p); !web.empty()) {
        adoptLinkOptions(o, web);
      } else {
        o.file = p;
      }
    }
    return o;
  }

  void adoptLinkOptions(LaunchOptions& o, const LaunchOptions& link) {
    o.serverUrl = link.serverUrl;
    o.serverProjectId = link.serverProjectId;
    o.serverVersion = link.serverVersion;
    if (!link.project.isEmpty()) o.project = link.project;
    if (!link.src.isEmpty()) {
      o.src = link.src;
      o.srcFromLink = link.srcFromLink;
    }
    if (!link.layout.isEmpty()) {
      o.layout = link.layout;
      o.layoutFromLink = link.layoutFromLink;
    }
    if (!link.layoutJson.isEmpty()) o.layoutJson = link.layoutJson;
    if (link.frame > 0) o.frame = link.frame;
    o.incognito = o.incognito || link.incognito;
    if (link.hasTheme) {
      o.hasTheme = true;
      o.theme = link.theme;
    }
    o.connect = link.connect;
    o.script = link.script;
    o.scriptDropped = link.scriptDropped;
  }

  void adoptLaunchUri(LaunchOptions& o, const launchUri::LaunchUri& link) {
    if (link.picture) std::visit(PictureAdopter{o, link}, *link.picture);
    if (link.layout) {
      if (link.layout->data) {
        // Bounded: parsed as JSON downstream, so the same cap as the browser hand-off payload.
        const QString json = QString::fromUtf8(QJsonDocument(*link.layout->data).toJson(QJsonDocument::Compact));
        if (json.size() <= deepLink::browserLaunchPayloadMax()) o.layoutJson = json;
      } else {
        o.layout = link.layout->url;
        o.layoutFromLink = true;
      }
    }
    o.incognito = o.incognito || link.incognito;
    if (std::any_of(link.warnings.begin(), link.warnings.end(),
                    [](const launchUri::Warning& w) { return w.key == QLatin1String("script"); }))
      o.scriptDropped = true;
    if (!link.script.isEmpty()) o.script = link.script;
    if (link.theme) {
      o.hasTheme = true;
      o.theme = *link.theme;
    }
    o.connect = link.connect;
  }

  LaunchOptions parseStencilUrl(const QUrl& url) {
    LaunchOptions o;
    if (url.scheme().compare(QLatin1String("stencil"), Qt::CaseInsensitive) != 0)
      return o;
    if (const auto link = launchUri::parse(url)) adoptLaunchUri(o, *link);
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
