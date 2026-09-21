#pragma once
// Native OS share sheet (browser parity: js/core/service.js shareImage() — same
// file name "<base>-drawing.png", same title "<base> — Stencil"). One declaration,
// three bodies picked at configure time: shareImageMac.mm, shareImageWin.cpp,
// shareImageLinux.cpp (no OS share sheet; isShareSheetAvailable() is false there).
#include <QString>

class QWidget;

namespace stencil::support {

  // Where false the Share action is hidden, as the browser hides its button when navigator.canShare() refuses files.
  bool isShareSheetAvailable();

  // `filePath` is already written. `anchor` positions a popover where the platform shows
  // one. False only when nothing could be shown at all.
  bool showShareSheet(QWidget* anchor, const QString& filePath, const QString& title);

}  // namespace stencil::support
