#pragma once
// Private seam between the tipContent TUs: the two key-rendering helpers the render and wiring
// halves reach for. Not part of the public tipContent.hpp surface.
#include "tipContent.hpp"

namespace stencil::gui::tipdetail {

  QString keysHtml(const QString& combo, const Palette& pal, bool mac, qreal scale = 1.0);
  QString highlightKeys(const QString& text, const Palette& pal, bool mac);

}  // namespace stencil::gui::tipdetail
