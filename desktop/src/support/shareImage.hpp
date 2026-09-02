#pragma once
// Native OS share sheet for the annotated image (dataExportController.cpp shareImage,
// browser/extension parity: js/core/exportService.js shareImage() — same file name
// "<base>-drawing.png", same title "<base> — Stencil").
//
// One declaration, three bodies — chosen at configure time (desktop/CMakeLists.txt) since
// the native type behind each is only available on its own platform:
//   shareImageMac.mm    macOS   — NSSharingServicePicker (AirDrop, Mail, Notes, …)
//   shareImageWin.cpp   Windows — DataTransferManager (WinRT: Mail, Nearby Share, …)
//   shareImageLinux.cpp Linux   — no OS share sheet exists across desktop environments;
//                                 reveals the file in the system file manager instead.
#include <QString>

class QWidget;

namespace stencil::support {

  // Shows the OS's share UI (or, on Linux, reveals the file) for the file already
  // written at `filePath` — the caller has already rendered and saved it, this only
  // hands it off. `anchor` positions a popover on platforms that show one near a
  // control; `title` is the share sheet's subject line where the OS displays one.
  // Returns false only when nothing could be shown at all (a missing file, or no
  // desktop integration available) — the caller reports that as "not supported here".
  bool showShareSheet(QWidget* anchor, const QString& filePath, const QString& title);

}  // namespace stencil::support
