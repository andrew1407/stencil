// Linux body of shareImage.hpp. There is no cross-desktop OS share sheet on Linux —
// no GNOME/KDE/XFCE-spanning equivalent of NSSharingServicePicker or
// DataTransferManager, and xdg-desktop-portal ships no general "share this file"
// portal (only narrow ones like email/URI). So there is nothing to show, and the
// Share action is hidden here instead of offered and then refused — the browser does
// exactly that with its own button when navigator.canShare() turns files down
// (browser/js/utils.js supportsShareFiles → toolbar.js / contextMenu.js).
//
// showShareSheet stays defined for the shared declaration and answers false: with the
// action hidden nothing reaches it, and a caller that gets here anyway (a scripted
// trigger) gets the "Sharing not supported on this system" toast rather than a
// surprise — the same shape as the browser's "Sharing not supported on this browser".
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
