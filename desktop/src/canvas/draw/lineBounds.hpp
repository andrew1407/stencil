#pragma once
// Each line's padded bounds in image space, kept across the paints no edit asked for (hover rings,
// flights) so they cull the lines without walking every point; any other paint, or an edit, refills it.
#include <QRect>
#include <QRectF>
#include <vector>

namespace stencil::gui {

  struct LineBounds {
    struct Entry {
      QRectF image;            // image px, padded for stroke, rings and flights
      std::size_t points = 0;  // what it was taken from: a different count or look is a stale entry
      double thickness = 0.0;
      double pointSize = 0.0;
    };
    std::vector<Entry> lines;
    bool valid = false;
    QRect asked;   // what the non-editing repaints asked for since the last paint
    void clear() { valid = false; }
    void ask(const QRect& r) { asked = asked.united(r); }
    // A paint past what those repaints asked for is some edit's, and re-takes every line; returns `exposed`.
    const QRect& painting(const QRect& exposed) {
      if (asked.isNull() || !asked.contains(exposed)) clear();
      asked = QRect();
      return exposed;
    }
  };

}  // namespace stencil::gui
