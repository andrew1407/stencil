#pragma once
#include <QImage>

// The picture as the backing store blends it (premultiplied ARGB32), converted once per source
// image instead of on every paint; a new or detached source re-keys it.
namespace stencil::gui {

  class PremulImage {
   public:
    const QImage& of(const QImage& src) {
      if (src.cacheKey() != key) {
        key = src.cacheKey();
        premul = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
      }
      return premul;
    }

   private:
    qint64 key = 0;
    QImage premul;
  };

}  // namespace stencil::gui
