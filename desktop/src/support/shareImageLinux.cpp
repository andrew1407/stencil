// Linux body of shareImage.hpp: no cross-desktop share sheet exists (xdg-desktop-portal
// has none), so the Share action is hidden here — the browser does the same when
// navigator.canShare() turns files down. showShareSheet answers false for a scripted trigger.
#include "shareImage.hpp"

class QWidget;

namespace stencil::support {

  bool shareSheetAvailable() { return false; }

  bool showShareSheet(QWidget* anchor, const QString& filePath, const QString& title) {
    (void)anchor;
    (void)filePath;
    (void)title;
    return false;
  }

}  // namespace stencil::support
