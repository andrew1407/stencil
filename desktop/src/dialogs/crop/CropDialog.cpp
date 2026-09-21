#include "CropDialog.hpp"
#include "cropDialogParts.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/motionPrefs.hpp"
#include <QEasingCurve>
#include <QGuiApplication>
#include <QVariantAnimation>
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
                           QWidget* parent, bool autoFitScreen)
      : QWidget(parent),
        original(original),
        pageWidthCm(pageWidthCm),
        pageHeightCm(pageHeightCm) {
    iw = this->original.width();
    ih = this->original.height();
    album = core::isAlbumOrientation(
        initial.width > 0 ? initial.width : iw,
        initial.height > 0 ? initial.height : ih);
    aspect = core::cropAspect(this->pageWidthCm, this->pageHeightCm, album);
    cropBox = initial.width > 0 ? initial : core::centeredCrop(iw, ih, aspect);

    if (autoFitScreen) setFitBox(previewFitBox(screenAvail(parent)));
    setMouseTracking(true);
  }

  void CropPreview::setOriginal(const QImage& original) {
    if (original.isNull()) return;
    const bool sameSize = original.width() == iw && original.height() == ih;
    this->original = original;
    iw = this->original.width();
    ih = this->original.height();
    if (!sameSize) {
      settleRect();
      cropBox = core::centeredCrop(iw, ih, aspect);
      // The box last FIT INTO, not scale * the new pixels: scale is the previous image's ratio, so a
      // portrait swapped for a landscape landed the widget far outside PREVIEW_MAX_W/H.
      setFitBox(fitBox);
      emit cropChanged();
    }
    update();
  }

  // Fit the original into the box (modest upscaling keeps small images' handles usable). A degenerate
  // box must NEVER fall back to scale 1.0 - that is NATIVE pixels, which dwarf the dialog.
  void CropPreview::setFitBox(const QSize& box) {
    if (box.width() <= 0 || box.height() <= 0) return;   // keep the last good fit
    fitBox = box;
    const double s = std::min(static_cast<double>(box.width()) / std::max(1, iw),
                              static_cast<double>(box.height()) / std::max(1, ih));
    scale = s > 0 ? s : 1.0;
    setFixedSize(qRound(iw * scale) + 2 * INSET, qRound(ih * scale) + 2 * INSET);
    update();
  }

  // swapCropOrientation carries the user's own framing across the flip. Browser twin:
  // openImageModal.js recenterCrop / cropModal.js recenter.
  void CropPreview::setAlbum(bool album) {
    // A same-value call (the constructor's own bootstrap) must stay idempotent - swapping an
    // already-correct rect would put it at the WRONG, reciprocal aspect.
    if (album == this->album) { update(); emit cropChanged(); return; }
    const core::CropRect from = cropBox;
    this->album = album;
    aspect = core::cropAspect(pageWidthCm, pageHeightCm, this->album);
    cropBox = core::swapCropOrientation(cropBox, aspect, iw, ih);
    flyRectFrom(from);
    emit cropChanged();
  }

  // The flip's own flight: the painted box eases from its old shape while cropBox is already the new
  // one (browser twin: rectTween.js). A widget not yet shown lands at once.
  void CropPreview::flyRectFrom(const core::CropRect& from) {
    if (!isVisible() || support::motionReduced() || from.width <= 0) { settleRect(); return; }
    if (!rectAnim) {
      rectAnim = new QVariantAnimation(this);
      rectAnim->setDuration(CROP_TWEEN_MS);
      rectAnim->setEasingCurve(QEasingCurve::OutCubic);
      connect(rectAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const QRectF r = v.toRectF();
        shownRect = {r.x(), r.y(), r.width(), r.height()};
        update();
      });
      connect(rectAnim, &QVariantAnimation::finished, this, &CropPreview::settleRect);
    }
    rectAnim->stop();
    flying = true;
    shownRect = from;
    rectAnim->setStartValue(QRectF(from.x, from.y, from.width, from.height));
    rectAnim->setEndValue(QRectF(cropBox.x, cropBox.y, cropBox.width, cropBox.height));
    rectAnim->start();
  }

  void CropPreview::settleRect() {
    flying = false;
    if (rectAnim && rectAnim->state() != QAbstractAnimation::Stopped) rectAnim->stop();
    update();
  }

  // A DIFFERENT page has no reciprocal to carry the old box across, so this resets to a fresh
  // default at the new aspect. Browser twin: openImageModal.js applyCropPageChange.
  void CropPreview::setPageSize(double pageWidthCm, double pageHeightCm) {
    this->pageWidthCm = pageWidthCm;
    this->pageHeightCm = pageHeightCm;
    aspect = core::cropAspect(this->pageWidthCm, this->pageHeightCm, album);
    cropBox = core::centeredCrop(iw, ih, aspect);
    settleRect();
    emit cropChanged();
  }

  core::Point CropPreview::toImage(const QPoint& w) const {
    return {(w.x() - INSET) / scale, (w.y() - INSET) / scale};
  }

  QRect CropPreview::imageRect() const {
    return rect().adjusted(INSET, INSET, -INSET, -INSET);
  }

  QRectF CropPreview::displayRect() const {
    const core::CropRect& r = flying ? shownRect : cropBox;
    return QRectF(INSET + r.x * scale, INSET + r.y * scale, r.width * scale, r.height * scale);
  }

  int CropPreview::cornerAt(const QPoint& wp) const {
    const QRectF d = displayRect();
    const QPointF corners[4] = {d.topLeft(), d.topRight(), d.bottomRight(),
                                d.bottomLeft()};
    for (int i = 0; i < 4; ++i) {
      if (std::hypot(wp.x() - corners[i].x(), wp.y() - corners[i].y()) <=
          HANDLE + 4)
        return i;
    }
    return -1;
  }

  void CropPreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(imageRect(), original);

    const QRectF d = displayRect();
    // Dim everything outside the crop (even-odd fill of the image rect minus crop).
    QPainterPath outside;
    outside.setFillRule(Qt::OddEvenFill);
    outside.addRect(QRectF(imageRect()));
    outside.addRect(d);
    p.fillPath(outside, QColor(0, 0, 0, SHADE_ALPHA));

    // The browser's 2px border sits INSIDE the crop box (border-box), so inset by 1.
    const QColor accent = cropAccent(this);
    QPen pen(accent);
    pen.setWidth(2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(d.adjusted(1, 1, -1, -1));

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(accent);
    p.setPen(QPen(Qt::white, 2));
    const QPointF corners[4] = {d.topLeft(), d.topRight(), d.bottomRight(),
                                d.bottomLeft()};
    for (const auto& c : corners) p.drawEllipse(c, HANDLE, HANDLE);
  }
}

