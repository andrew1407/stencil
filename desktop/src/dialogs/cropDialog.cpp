#include "cropDialog.hpp"
#include "cropDialogParts.hpp"
#include "../support/modalChrome.hpp"
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <algorithm>
#include <cmath>

namespace stencil::gui {


  CropPreview::CropPreview(const QImage& original, double pageWidthCm,
                           double pageHeightCm, const core::CropRect& initial,
                           QWidget* parent)
      : QWidget(parent),
        original_(original),
        pageWidthCm_(pageWidthCm),
        pageHeightCm_(pageHeightCm) {
    iw_ = original_.width();
    ih_ = original_.height();
    album_ = core::isAlbumOrientation(
        initial.width > 0 ? initial.width : iw_,
        initial.height > 0 ? initial.height : ih_);
    aspect_ = core::cropAspect(pageWidthCm_, pageHeightCm_, album_);
    rect_ = initial.width > 0 ? initial : core::centeredCrop(iw_, ih_, aspect_);

    setFitBox(previewFitBox(screenAvail(parent)));
    setMouseTracking(true);
  }

  // Fit the original into the box (allow modest upscaling of small images so the
  // handles are usable); the widget takes exactly the scaled image plus the handle inset.
  void CropPreview::setFitBox(const QSize& box) {
    const double s = std::min(static_cast<double>(box.width()) / std::max(1, iw_),
                              static_cast<double>(box.height()) / std::max(1, ih_));
    scale_ = s > 0 ? s : 1.0;
    setFixedSize(qRound(iw_ * scale_) + 2 * kInset, qRound(ih_ * scale_) + 2 * kInset);
    update();
  }

  void CropPreview::setAlbum(bool album) {
    album_ = album;
    aspect_ = core::cropAspect(pageWidthCm_, pageHeightCm_, album_);
    rect_ = core::centeredCrop(iw_, ih_, aspect_);
    update();
    emit cropChanged();
  }

  core::Point CropPreview::toImage(const QPoint& w) const {
    return {(w.x() - kInset) / scale_, (w.y() - kInset) / scale_};
  }

  QRect CropPreview::imageRect() const {
    return rect().adjusted(kInset, kInset, -kInset, -kInset);
  }

  QRectF CropPreview::displayRect() const {
    return QRectF(kInset + rect_.x * scale_, kInset + rect_.y * scale_,
                  rect_.width * scale_, rect_.height * scale_);
  }

  int CropPreview::cornerAt(const QPoint& wp) const {
    const QRectF d = displayRect();
    const QPointF corners[4] = {d.topLeft(), d.topRight(), d.bottomRight(),
                                d.bottomLeft()};
    for (int i = 0; i < 4; ++i) {
      if (std::hypot(wp.x() - corners[i].x(), wp.y() - corners[i].y()) <=
          kHandle + 4)
        return i;
    }
    return -1;
  }

  void CropPreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(imageRect(), original_);

    const QRectF d = displayRect();
    // Dim everything outside the crop (even-odd fill of the image rect minus crop).
    QPainterPath outside;
    outside.setFillRule(Qt::OddEvenFill);
    outside.addRect(QRectF(imageRect()));
    outside.addRect(d);
    p.fillPath(outside, QColor(0, 0, 0, kShadeAlpha));

    // The browser's 2px border sits INSIDE the crop box (border-box), so inset by 1.
    QPen pen(kCropAccent);
    pen.setWidth(2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(d.adjusted(1, 1, -1, -1));

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(kCropAccent);
    p.setPen(QPen(Qt::white, 2));
    const QPointF corners[4] = {d.topLeft(), d.topRight(), d.bottomRight(),
                                d.bottomLeft()};
    for (const auto& c : corners) p.drawEllipse(c, kHandle, kHandle);
  }
}

