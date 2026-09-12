#pragma once
// The loader's video timeouts and scheme/extension tests, private to the MediaLoader*.cpp TUs.

#include <QString>
#include "MediaLoader.hpp"
#include <QUrl>

namespace stencil::gui {

  inline constexpr int VIDEO_TIMEOUT_MS = 20000;  // give the decoder time to seek+render
  inline constexpr double ASSUMED_FPS = 30.0;    // fallback when fps metadata is absent

  inline bool isHttp(const QUrl& u) {
    const QString s = u.scheme();
    return s == "http" || s == "https";
  }

  // Video by container extension (the launch arg's suffix, or the URL path's).
  inline bool looksLikeVideo(const QString& src, const QUrl& url) {
    return isVideoFileName(src) || isVideoFileName(url.path());
  }

}  // namespace stencil::gui
