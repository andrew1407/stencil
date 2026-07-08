#pragma once
#include <QString>

class QCoreApplication;
class QUrl;

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

    // --incognito : edit without saving. Honored whenever we are NOT opening a
    // saved --project (a blank incognito editor, or an incognito image --src);
    // ignored for --project, mirroring the browser, where incognito rides along
    // the external-image launch, not a project deep-link.
    bool incognito = false;

    // --layout <path|url> : a layout JSON to apply once a --src image has loaded
    // successfully (local file or URL). Ignored without --src.
    QString layout;

    // --projects : open the Projects window at launch.
    bool projects = false;

    // Quick-crop override for the resolved --src image. NOT parsed from argv — set only
    // by the Open-Image dialog's "Open in new window" handoff so the fresh window
    // applies the same crop the user chose in the preview. hasCropOverride selects it;
    // cropToPage crops centered to cropPage in cropAlbum/portrait, else the whole frame
    // loads uncropped (crop toggle off with a preview). Mirrors the LinksDialog model.
    bool hasCropOverride = false;
    bool cropToPage = false;
    bool cropAlbum = false;
    QString cropPage;

    // A bare positional file argument (image / video / layout JSON), the form an
    // OS file-association or "Open With" hands the app. Routed through the same
    // suffix-sniffing open path as drag-and-drop. Lower priority than --src.
    QString file;

    // ── stencil:// deep-link fields (parseStencilUrl) ──
    // serverUrl+serverProjectId: open that project from that collaboration server,
    // connecting like a fresh client (reuse a saved token, else mint one via
    // POST /auth/token — no token ever rides the link). Wins over src/file.
    QString serverUrl;
    QString serverProjectId;
    qint64 serverVersion = 0;
    // Inline layout JSON (the `layout=` query param), applied once the src image
    // loads — the in-URL variant of --layout for browser→desktop hand-offs.
    QString layoutJson;

    // True when nothing was requested (a plain launch) — applyLaunchOptions then
    // does nothing and the normal session-restore stands.
    bool empty() const {
      return !hasTheme && project.isEmpty() && src.isEmpty() && layout.isEmpty() &&
             !projects && !incognito && file.isEmpty() && serverUrl.isEmpty() &&
             serverProjectId.isEmpty() && layoutJson.isEmpty();
    }
  };

  // Parse the application's arguments into LaunchOptions. Uses QCommandLineParser
  // (exact long-option matching, so --project and --projects stay distinct) and
  // tolerates malformed values by falling back to defaults. A stencil:// positional
  // (the OS scheme handler's argv form on Linux) routes through parseStencilUrl.
  LaunchOptions parseLaunchOptions(const QCoreApplication& app);

  // Parse a `stencil://open?…` deep link (the inbound side of the cross-front-end
  // "Open in…" feature; grammar mirrored by browser/js/core/deepLink.js
  // buildStencilSchemeUrl). Recognized query params: server=<origin|host[:port]> +
  // id=<projectId> [+ version=<n>] | src=<http(s) url|data:> [+ layout=<inline JSON>]
  // [+ frame=<n>], plus incognito=1. server+id win over src; unknown params are
  // ignored; a non-stencil URL yields empty options. src is restricted to web/data
  // sources — links are remotely clickable, so local paths never ride them.
  LaunchOptions parseStencilUrl(const QUrl& url);

}
