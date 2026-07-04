#include "launchOptions.hpp"
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QUrl>
#include <QUrlQuery>

namespace stencil::gui {

  LaunchOptions parseLaunchOptions(const QCoreApplication& app) {
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Stencil — image annotation / drawing tool");
    parser.addHelpOption();

    // Value options carry a single argument; the bare flags (--incognito,
    // --projects) are valueless. Names are matched exactly, so --project (value)
    // and --projects (flag) never collide.
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
    // A bare file path (what a file-association / "Open With" passes), opened via
    // the same suffix-sniffing path as a drag-and-drop.
    parser.addPositionalArgument("file", "Image, video, or layout JSON to open.",
                                 "[file]");

    // process() honors --help/--version and exits on a malformed command line,
    // which is the conventional CLI behavior (the GUI only starts on success).
    parser.process(app);

    LaunchOptions o;
    if (parser.isSet(themeOpt)) {
      const QString t = parser.value(themeOpt).trimmed().toLower();
      // Only the two explicit modes are accepted; anything else is ignored so we
      // don't clobber the saved/system preference with a typo.
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
      // A stencil:// deep link (Linux scheme handlers pass the URL as argv %u).
      // Its fields fold into the options; other flags (--theme etc.) still apply.
      if (p.startsWith(QLatin1String("stencil:"), Qt::CaseInsensitive)) {
        const LaunchOptions s = parseStencilUrl(QUrl(p));
        o.serverUrl = s.serverUrl;
        o.serverProjectId = s.serverProjectId;
        o.serverVersion = s.serverVersion;
        if (!s.src.isEmpty()) o.src = s.src;
        if (!s.layoutJson.isEmpty()) o.layoutJson = s.layoutJson;
        if (s.frame > 0) o.frame = s.frame;
        o.incognito = o.incognito || s.incognito;
      } else {
        o.file = p;
      }
    }
    return o;
  }

  LaunchOptions parseStencilUrl(const QUrl& url) {
    LaunchOptions o;
    if (url.scheme().compare(QLatin1String("stencil"), Qt::CaseInsensitive) != 0)
      return o;
    const QUrlQuery q(url);
    const QString server = q.queryItemValue("server", QUrl::FullyDecoded).trimmed();
    const QString id = q.queryItemValue("id", QUrl::FullyDecoded).trimmed();
    if (!server.isEmpty() && !id.isEmpty()) {
      // A server reference wins over any inline content (the server copy is canonical).
      o.serverUrl = server;
      o.serverProjectId = id;
      bool ok = false;
      const qint64 v = q.queryItemValue("version").toLongLong(&ok);
      o.serverVersion = (ok && v > 0) ? v : 0;
    } else {
      o.src = q.queryItemValue("src", QUrl::FullyDecoded).trimmed();
      // Deep links are remotely clickable, so only web/data image sources may ride
      // them — a crafted link must not open arbitrary LOCAL files. Local paths stay
      // a --src / positional-argument capability (user-initiated by definition).
      if (!o.src.isEmpty()
          && !o.src.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
          && !o.src.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)
          && !o.src.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
        o.src.clear();
      }
      o.layoutJson = q.queryItemValue("layout", QUrl::FullyDecoded).trimmed();
      bool ok = false;
      const int n = q.queryItemValue("frame").toInt(&ok);
      o.frame = (ok && n > 0) ? n : 0;
    }
    o.incognito = q.queryItemValue("incognito") == QLatin1String("1");
    return o;
  }

}
