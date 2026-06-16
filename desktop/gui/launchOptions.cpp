#include "launchOptions.hpp"
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>

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
    return o;
  }

}
