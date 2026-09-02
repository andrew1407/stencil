// Linux body of shareImage.hpp. There is no cross-desktop OS share sheet on Linux —
// no GNOME/KDE/XFCE-spanning equivalent of NSSharingServicePicker or
// DataTransferManager, and xdg-desktop-portal ships no general "share this file"
// portal (only narrow ones like email/URI). The honest fallback is to reveal the
// file where the user can decide what to do with it themselves, the same gesture
// a "Show in Folder" action gives.
//
// Plain QDesktopServices, not the org.freedesktop.FileManager1 D-Bus interface some
// file managers (Nautilus, Dolphin, …) answer with a file PRE-SELECTED: that needs
// QtDBus, a Qt module this app otherwise never links, and — since there is no
// Linux desktop session in this repo to verify ShowItems actually lands — the
// dependency isn't worth taking for a nicety on top of a fallback that is already
// the "not really shared" branch. Opening the containing folder needs nothing new.
//
// `title` has nowhere to go here — nothing is being handed a subject line, just a
// folder window — so it stays unused, kept only for parity with the other two
// platforms' signature.
#include "shareImage.hpp"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>
#include <QWidget>

namespace stencil::support {

  bool showShareSheet(QWidget* anchor, const QString& filePath, const QString& title) {
    (void)anchor;
    (void)title;
    const QFileInfo info(filePath);
    if (!info.exists()) return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
  }

}  // namespace stencil::support
