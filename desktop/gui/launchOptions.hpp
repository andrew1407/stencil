#pragma once
#include <QString>

class QCoreApplication;

// Command-line launch options for the desktop app (the executable counterpart of
// the browser's URL deep-links — applyExternalLaunch '#stencil=' and
// applyProjectDeepLink '?open='). Parsed once in main() and handed to
// MainWindow::applyLaunchOptions() after the window is shown so the async
// network / video resolution can run on the event loop.
namespace stencil::gui {

  struct LaunchOptions {
    // --theme dark|light : explicitly set (and persist as) the default theme for
    // this launch, overriding the saved/system choice. hasTheme gates it so an
    // absent flag leaves the persisted preference untouched.
    bool hasTheme = false;
    QString theme;  // "dark" | "light"

    // --project <name> : open an existing, editable saved project by name
    // (case-insensitive). Takes precedence over --src when both are given.
    QString project;

    // --src <path|url> : open an image by local path, fetch+open a remote image
    // URL, or grab a frame from a video file/URL. Mutually exclusive with
    // --project (project wins).
    QString src;

    // --frame <n> : 0-based video frame to open in the editor. Defaults to the
    // first frame; ignored for still images. Negative/invalid clamps to 0.
    int frame = 0;

    // --incognito : edit without saving. Only honored when a fresh image --src is
    // opened (never for a saved --project), mirroring the browser, where incognito
    // rides along the external-image launch payload, not a project deep-link.
    bool incognito = false;

    // --layout <path|url> : a layout JSON to apply once a --src image has loaded
    // successfully (local file or URL). Ignored without --src.
    QString layout;

    // --projects : open the Projects window at launch.
    bool projects = false;

    // True when nothing was requested (a plain launch) — applyLaunchOptions then
    // does nothing and the normal session-restore stands.
    bool empty() const {
      return !hasTheme && project.isEmpty() && src.isEmpty() && layout.isEmpty() &&
             !projects;
    }
  };

  // Parse the application's arguments into LaunchOptions. Uses QCommandLineParser
  // (exact long-option matching, so --project and --projects stay distinct) and
  // tolerates malformed values by falling back to defaults.
  LaunchOptions parseLaunchOptions(const QCoreApplication& app);

}
