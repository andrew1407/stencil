#include "imageTurn.hpp"

#include "imageOps.hpp"   // core/: this file is the seam that may include it

#include <QColorSpace>
#include <QTransform>

namespace stencil::model {

  namespace {
    // A pixel is four bytes in every depth-32 format, which is all core's kernels assume.
    bool fourBytes(const QImage& img) {
      return img.depth() == 32 && img.bytesPerLine() == img.width() * 4;
    }

    QImage blankLike(const QImage& from, const QSize& size) {
      QImage out(size, from.format());
      out.setDotsPerMeterX(from.dotsPerMeterX());
      out.setDotsPerMeterY(from.dotsPerMeterY());
      out.setColorSpace(from.colorSpace());
      return out;
    }
  }  // namespace

  QSize turnedSize(const QSize& size, int quarters) {
    int w = 0, h = 0;
    core::rotatedDims(size.width(), size.height(), quarters, w, h);
    return {w, h};
  }

  QRect sourceRectOf(const QRect& t, int quarters, const QSize& original) {
    const int w = original.width(), h = original.height();
    switch (core::normalizeQuarters(quarters)) {
      case 1: return QRect(t.y(), h - t.x() - t.width(), t.height(), t.width());
      case 2: return QRect(w - t.x() - t.width(), h - t.y() - t.height(), t.width(), t.height());
      case 3: return QRect(w - t.y() - t.height(), t.x(), t.height(), t.width());
      default: return t;
    }
  }

  QImage turn(const QImage& original, int quarters) {
    const int q = core::normalizeQuarters(quarters);
    if (q == 0 || original.isNull()) return original;
    if (!fourBytes(original)) return original.transformed(QTransform().rotate(q * 90.0));
    QImage out = blankLike(original, turnedSize(original.size(), q));
    core::rotateImageRows(original.constBits(), original.width(), original.height(), q, out.bits(),
                          0, out.height());
    return out;
  }

  QImage mirror(const QImage& original) {
    if (original.isNull()) return original;
    if (!fourBytes(original)) return original.transformed(QTransform().scale(-1, 1));
    QImage out = blankLike(original, original.size());
    core::mirrorImageRows(original.constBits(), original.width(), original.height(), out.bits(), 0,
                          out.height());
    return out;
  }

  QImage turnAndCrop(const QImage& original, int quarters, const QRect& crop) {
    if (original.isNull()) return QImage();
    const int q = core::normalizeQuarters(quarters);
    const QRect clipped = crop.intersected(QRect(QPoint(0, 0), turnedSize(original.size(), q)));
    // QImage::copy(QRect()) is the whole picture, and the canvas always read it that way.
    if (clipped.isNull()) return turn(original, q);
    const QRect src = sourceRectOf(clipped, q, original.size());
    if (!fourBytes(original)) return turn(original.copy(src), q);
    QImage piece = blankLike(original, src.size());
    core::cropImageRows(original.constBits(), original.width(), original.height(), src.x(), src.y(),
                        src.width(), src.height(), piece.bits(), 0, src.height());
    return turn(piece, q);
  }

}  // namespace stencil::model
