#pragma once
#include <QString>

class QCoreApplication;
class QUrl;

// Command-line launch options — the desktop twin of the browser's URL deep-links (applyExternalLaunch / applyProjectDeepLink).
// Applied after the window is shown so async network / video resolution runs on the event loop.
namespace stencil::gui {

  struct LaunchOptions {
    // --theme dark|light; hasTheme gates it so an absent flag leaves the persisted preference untouched.
    bool hasTheme = false;
    QString theme;  // "dark" | "light"

    // --project <name> (case-insensitive); wins over --src.
    QString project;

    // --src <path|url>: local path, remote image URL, or a video frame.
    QString src;

    // --frame <n>: 0-based; negative/invalid clamps to 0.
    int frame = 0;

    // --incognito is ignored for --project, as in the browser (it rides the external-image launch, not a project link).
    bool incognito = false;

    // --layout <path|url>: applied once a --src image has loaded.
    QString layout;

    bool projects = false;

    // NOT parsed from argv — set by the Open-Image dialog's "Open in new window" handoff. Mirrors the LinksDialog model.
    bool hasCropOverride = false;
    bool cropToPage = false;
    bool cropAlbum = false;
    QString cropPage;
    // The rect the crop stage was left on, in original-image pixels; width 0 ⇒ nothing was
    // dragged and the page crop centres. Plain doubles: this header stays off the core seam.
    double cropX = 0, cropY = 0, cropW = 0, cropH = 0;

    // A bare positional file (OS "Open With"); routed through the drag-and-drop open path. Lower priority than --src.
    QString file;

    // stencil:// deep-link fields (parseStencilUrl). A saved token is reused, else minted via POST /auth/token — no token rides the link.
    QString serverUrl;
    QString serverProjectId;
    qint64 serverVersion = 0;
    // Inline layout JSON (`layout=`), the in-URL variant of --layout.
    QString layoutJson;

    // A plain launch: applyLaunchOptions does nothing and the session restore stands.
    bool empty() const {
      return !hasTheme && project.isEmpty() && src.isEmpty() && layout.isEmpty() &&
             !projects && !incognito && file.isEmpty() && serverUrl.isEmpty() &&
             serverProjectId.isEmpty() && layoutJson.isEmpty();
    }
  };

  // QCommandLineParser (exact long-option matching keeps --project / --projects distinct); a stencil:// positional routes through parseStencilUrl.
  LaunchOptions parseLaunchOptions(const QCoreApplication& app);

  // Grammar mirrored by browser/js/core/deepLink.js buildStencilSchemeUrl: server+id win over src; unknown params ignored;
  // src is restricted to web/data sources — links are remotely clickable, so local paths never ride them.
  LaunchOptions parseStencilUrl(const QUrl& url);

}
