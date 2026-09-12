#include "mainWindowShared.hpp"

#include "../support/DisintegrateOverlay.hpp"
#include "fetchGuard.hpp"

#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <memory>
#include <utility>

namespace stencil::gui {

  void fetchUrlBytesAsync(QObject* ctx, const QString& url,
                          std::function<void(QByteArray)> done) {
    // The URL comes off a shared project record, so it is untrusted: the strict guard, a capped
    // body, no redirect off the host. Failure lands as empty bytes.
    stencil::net::fetchGuard::get(
        ctx, QUrl(url), /*strict=*/true,
        [done = std::move(done)](const QByteArray& b, const QString&) { done(b); }, 10000);
  }

  void parseLayoutFilter(const QJsonObject& layout, const QString& defTint,
                         QString& filter, QString& tint) {
    filter = layout.value("imageFilter")
                 .toString(layout.value("blackAndWhite").toBool(false) ? "bw" : "none");
    tint = layout.value("filterColor").toString(defTint);
  }

  std::function<void(int)> pinAndRaiseDust(std::function<void(int)> pin,
                                           const QPointer<gui::DisintegrateOverlay>& fx) {
    auto pending = std::make_shared<bool>(false);
    return [pin = std::move(pin), fx, pending](int v) {
      pin(v);
      if (!fx || *pending) return;
      *pending = true;
      QTimer::singleShot(0, fx, [fx, pending] {
        *pending = false;
        if (fx) fx->raise();
      });
    };
  }

}  // namespace stencil::gui
