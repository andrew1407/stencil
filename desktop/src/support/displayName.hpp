#pragma once
#include <QString>

// Names are often derived from a URL basename, and CDNs hand out opaque 60-char slugs
// ("MV5BODg3MzYwMjE4N15BMl5BanBnXkFtZTcwMjU5NzAzNw@@._V1_"). A list cell can elide on
// its own, but a name interpolated into a QMessageBox SENTENCE cannot — it wrapped
// across several lines and blew out the dialog.
//
// Desktop port of browser/js/utils.js `shortName` (extension/src/lib/displayName.js is
// the third). Keep all three behaviourally identical: same limit, same head/tail split.
// Middle ellipsis, because both ends carry meaning — the head is what little the user
// recognises and the tail holds the extension / "-copy" suffix saying WHICH item it is.
// Header-only and pure so the headless test drives it without a window.
namespace stencil::support {

  inline constexpr int kNameDisplayChars = 28;

  inline QString shortName(const QString& name, int limit = kNameDisplayChars) {
    if (name.size() <= limit) return name;
    // Reserve one char for the ellipsis; the extra char goes to the head on odd splits.
    const int keep = limit - 1;
    const int head = (keep + 1) / 2;   // ceil(keep / 2)
    const int tail = keep - head;
    return name.left(head) + QString::fromUtf8("…") + (tail > 0 ? name.right(tail) : QString());
  }

}  // namespace stencil::support
