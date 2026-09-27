// model/imageTurn over core's imageOps: every quarter-turn and crop lands the same pixels as
// Qt's own rotate-then-copy did, for four-byte pixels and the formats that fall back to Qt.
#include "imageTurn.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QTransform>

#include "../support/check.hpp"

namespace {
  // The canvas's old path: turn the whole picture, then copy the crop clipped to it.
  QImage viaQt(const QImage& img, int q, const QRect& crop) {
    const QImage rot = q == 0 ? img : img.transformed(QTransform().rotate(q * 90.0));
    return crop.isNull() ? rot : rot.copy(crop.intersected(rot.rect()));
  }

  QImage pattern(QImage::Format format) {
    QImage img(37, 23, QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y)
      for (int x = 0; x < img.width(); ++x)
        img.setPixel(x, y, qRgba(x * 7, y * 11, (x * y) % 256, 255 - x));
    return img.convertToFormat(format);
  }

  bool samePixels(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) return false;
    const QImage x = a.convertToFormat(QImage::Format_ARGB32);
    const QImage y = b.convertToFormat(QImage::Format_ARGB32);
    for (int r = 0; r < x.height(); ++r)
      for (int c = 0; c < x.width(); ++c)
        if (x.pixel(c, r) != y.pixel(c, r)) return false;
    return true;
  }
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  using namespace stencil::model;

  check(turnedSize(QSize(37, 23), 1) == QSize(23, 37) && turnedSize(QSize(37, 23), -2) == QSize(37, 23),
        "turnedSize is core::rotatedDims");
  const QRect crops[] = {QRect(3, 2, 11, 7), QRect(0, 0, 5, 30), QRect(10, 12, 40, 40)};
  for (const QImage::Format format : {QImage::Format_ARGB32, QImage::Format_RGB32,
                                      QImage::Format_RGBA8888, QImage::Format_RGB888,
                                      QImage::Format_Grayscale8}) {
    const QImage img = pattern(format);
    bool same = true;
    for (int q = -1; q <= 4; ++q) {
      same = same && samePixels(turn(img, q), viaQt(img, ((q % 4) + 4) % 4, QRect()));
      for (const QRect& crop : crops)
        same = same && samePixels(turnAndCrop(img, q, crop), viaQt(img, ((q % 4) + 4) % 4, crop));
    }
    std::printf("  format %d\n", int(format));
    check(same, "every turn and crop matches Qt's rotate-then-copy");
    check(turnAndCrop(img, 1, crops[0]).format() == img.format(), "…and keeps the picture's format");
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
