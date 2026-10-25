#pragma once
// Private seam between the tipContent TUs: the key-rendering helpers the render and wiring
// halves reach for, and the bound on the caps they paint. Not part of the public tipContent.hpp surface.
#include "tipContent.hpp"

namespace stencil::gui::tipdetail {

  QString keysHtml(const QString& combo, const Palette& pal, bool mac, qreal scale = 1.0);
  QString highlightKeys(const QString& text, const Palette& pal, bool mac);

  // Painted caps held, the least recently used dropped past the cap: every custom accent mints new ones.
  inline constexpr int KEYCAP_CACHE_CAP = 512;
  int keycapCacheSize();

}  // namespace stencil::gui::tipdetail
