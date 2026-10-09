#pragma once
// "Is this http(s)?", asked by every fetch, drop, script open and launch source; fetchGuard's
// refusals start from it. QtCore only, so a unit outside the network layer can ask too.
#include <QString>
#include <QUrl>

namespace stencil::net::fetchGuard {

  // A typed file:/smb: URL must never be fetched or handed to the OS URL handler.
  inline bool isWebScheme(const QUrl& url) {
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
  }

  // The same for text not yet parsed: an "http://" or "https://" prefix, any case.
  inline bool isWebScheme(const QString& spec) {
    return spec.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) ||
           spec.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
  }

}  // namespace stencil::net::fetchGuard
