#pragma once
// A picture QSS can only take by path (an indicator, a popup's arrows, the pinstripe tile), drawn once
// into the cache dir at 1x and 2x and found there after; empty where it cannot be written. Each family is
// pruned to its newest keys, since accents are open-ended.
#include <QPainter>
#include <QSize>
#include <QString>

#include <functional>

namespace stencil::support {

  // A family (`aq-ind`, `aq-stripe`) keeps this many keys, each its 1x and 2x file.
  inline constexpr int ART_CACHE_CAP = 32;
  void pruneArtFamily(const QString& dir, const QString& family);
  // `paint(p)` draws the 1x picture; the 2x is the same drawing scaled. Returns the 1x path.
  QString cachedArtPath(const QString& key, const QSize& size, const std::function<void(QPainter&)>& paint,
                        bool isRetina = true);

}  // namespace stencil::support
