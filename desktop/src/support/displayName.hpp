#pragma once
#include <QString>

// CDNs hand out opaque 60-char slugs; a name interpolated into a QMessageBox sentence
// (not a list cell, which elides on its own) wrapped across lines and blew out the dialog.
// Parity: browser-extension/src/lib/displayName.js and desktop/src/support/displayName.hpp are
// the third; keep all three behaviourally identical: same limit, same head/tail split.
// Header-only and pure so the headless test drives it without a window.
namespace stencil::support {

  inline constexpr int NAME_DISPLAY_CHARS = 28;

  inline QString shortName(const QString& name, int limit = NAME_DISPLAY_CHARS) {
    if (name.size() <= limit) return name;
    // Reserve one char for the ellipsis; the extra goes to the head on odd splits.
    const int keep = limit - 1;
    const int head = (keep + 1) / 2;   // ceil(keep / 2)
    const int tail = keep - head;
    return name.left(head) + QString::fromUtf8("…") + (tail > 0 ? name.right(tail) : QString());
  }

}  // namespace stencil::support
