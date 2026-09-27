#pragma once
// The loader's video timeouts and scheme/extension/signature tests, private to the MediaLoader*.cpp
// TUs.

#include <QString>
#include "MediaLoader.hpp"
#include <QUrl>

namespace stencil::gui {

  inline constexpr int VIDEO_TIMEOUT_MS = 20000;  // give the decoder time to seek+render
  inline constexpr double ASSUMED_FPS = 30.0;    // fallback when fps metadata is absent
  inline constexpr qsizetype SIGNATURE_BYTES = 12;  // RIFF....WEBP, the longest still-image signature

  // Bytes that carry a still image's signature and still will not decode have no stream behind
  // them, so they fail at once instead of waiting out the video probe.
  inline bool isStillImage(QByteArrayView head) { return !sniffImageHeader(head).format.isEmpty(); }

  inline QString unreadableMessage(const QString& src) {
    return QStringLiteral("Not a readable image or video: %1").arg(src);
  }

  inline bool isHttp(const QUrl& u) {
    const QString s = u.scheme();
    return s == "http" || s == "https";
  }

  // Video by container extension (the launch arg's suffix, or the URL path's).
  inline bool looksLikeVideo(const QString& src, const QUrl& url) {
    return isVideoFileName(src) || isVideoFileName(url.path());
  }

}  // namespace stencil::gui
