#pragma once
#include <QImage>
#include <QPainter>

#include <cmath>

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
    // A conversion made elsewhere (a pool thread) for `src`, so the first paint skips it.
    void seed(const QImage& src, const QImage& converted) {
      key = src.cacheKey();
      premul = converted;
    }

    // `src` at `target` (widget space, untransformed painter), scaled once per zoom: a dock slide moves the
    // canvas every frame and each paint re-scaled the whole picture. Past SCALED_CAP_PX it is drawn direct.
    void draw(QPainter& p, const QRectF& target, const QImage& src) {
      const QImage& from = of(src);
      const qreal dpr = p.device() ? p.device()->devicePixelRatio() : 1.0;
      const QSize px(static_cast<int>(std::ceil(target.width() * dpr)), static_cast<int>(std::ceil(target.height() * dpr)));
      if (qint64(px.width()) * px.height() > SCALED_CAP_PX || !p.transform().isIdentity()) {
        scaled = QImage();
        p.drawImage(target, from);
        return;
      }
      if (scaled.isNull() || scaledKey != key || scaledSize != target.size() || scaledDpr != dpr) {
        scaled = QImage(px, QImage::Format_ARGB32_Premultiplied);
        scaled.setDevicePixelRatio(dpr);
        scaled.fill(Qt::transparent);
        QPainter q(&scaled);
        q.setRenderHints(p.renderHints());
        q.drawImage(QRectF(QPointF(), target.size()), from);
        scaledKey = key;
        scaledSize = target.size();
        scaledDpr = dpr;
      }
      p.drawImage(target.topLeft(), scaled);
    }

   private:
    static constexpr qint64 SCALED_CAP_PX = 16LL << 20;   // device px, ~64 MB: a zoomed-in giant draws direct
    qint64 key = 0;
    QImage premul;
    qint64 scaledKey = 0;
    QSizeF scaledSize;
    qreal scaledDpr = 0;
    QImage scaled;
  };

}  // namespace stencil::gui
