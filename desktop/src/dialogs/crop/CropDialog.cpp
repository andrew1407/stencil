#include "../../support/skinPrefs.hpp"
#include "CropDialog.hpp"
#include "cropDialogParts.hpp"
#include "../../support/motionPrefs.hpp"
#include <QEasingCurve>
#include <QVariantAnimation>
#include <QPainter>
#include <QPainterPath>
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

    if (autoFitScreen) setPreferredBox(previewFitBox(screenAvail(parent)));
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
      // The box last FIT INTO, not the old hint: that is the previous image's shape, so a portrait
      // swapped for a landscape landed the widget far outside PREVIEW_MAX_W/H.
      if (pinned) {
        setFitBox(fitBox);
      } else {
        setPreferredBox(fitBox);
      }
      emit cropChanged();
    }
    update();
  }

  // Fit the original into the box (modest upscaling keeps small images' handles usable). A degenerate
  // box must NEVER fall back to scale 1.0 - that is NATIVE pixels, which dwarf the dialog.
  bool CropPreview::fitInto(const QSize& box) {
    if (box.width() <= 0 || box.height() <= 0) return false;   // keep the last good fit
    fitBox = box;
    const double s = std::min(static_cast<double>(box.width()) / std::max(1, iw),
                              static_cast<double>(box.height()) / std::max(1, ih));
    hint = QSize(qRound(iw * s) + 2 * INSET, qRound(ih * s) + 2 * INSET);
    return true;
  }

  void CropPreview::setFitBox(const QSize& box) {
    if (!fitInto(box)) return;
    pinned = true;
    setFixedSize(hint);
    update();
  }

  // The window's free room decides the size; the picture re-fits whatever it is (imageRect).
  void CropPreview::setPreferredBox(const QSize& box) {
    if (!fitInto(box)) return;
    pinned = false;
    setMinimumSize(STAGE_MIN + 2 * INSET, STAGE_MIN + 2 * INSET);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    updateGeometry();
    update();
  }

  QSize CropPreview::sizeHint() const {
    return hint.isValid() ? hint : QSize(STAGE_MIN + 2 * INSET, STAGE_MIN + 2 * INSET);
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

  double CropPreview::viewScale() const {
    if (iw <= 0 || ih <= 0) return 0.0;
    const double s = std::min((width() - 2.0 * INSET) / iw, (height() - 2.0 * INSET) / ih);
    return s > 0 ? s : 0.0;
  }

  // Whole-pixel top-left, so a pinned stage paints exactly inside its inset as before.
  QRectF CropPreview::imageRect() const {
    const double s = viewScale();
    const double w = iw * s, h = ih * s;
    return QRectF(std::floor((width() - w) / 2.0), std::floor((height() - h) / 2.0), w, h);
  }

  core::Point CropPreview::toImage(const QPoint& w) const {
    const double s = viewScale();
    if (s <= 0) return {0, 0};
    const QPointF at = imageRect().topLeft();
    return {(w.x() - at.x()) / s, (w.y() - at.y()) / s};
  }

  QRectF CropPreview::displayRect() const {
    const core::CropRect& r = flying ? shownRect : cropBox;
    const double s = viewScale();
    const QPointF at = imageRect().topLeft();
    return QRectF(at.x() + r.x * s, at.y() + r.y * s, r.width * s, r.height * s);
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
    const QRectF picture = imageRect();
    p.drawImage(picture, original);

    const QRectF d = displayRect();
    // Dim everything outside the crop (even-odd fill of the image rect minus crop).
    QPainterPath outside;
    outside.setFillRule(Qt::OddEvenFill);
    outside.addRect(picture);
    outside.addRect(d);
    p.fillPath(outside, QColor(0, 0, 0, SHADE_ALPHA));

    // The browser's 2px border sits INSIDE the crop box (border-box), so inset by 1.
    const QColor accent = cropAccent(this);
    QPen pen(accent);
    pen.setWidth(2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(d.adjusted(1, 1, -1, -1));

    // Under the skin nothing is round (browser webcore `border-radius: 0`): square handles in
    // its --accent-2, the title strip's second stop.
    const bool skin = support::isWebcore();
    p.setRenderHint(QPainter::Antialiasing, !skin);
    p.setBrush(skin ? support::skinBevel().titleB : accent);
    p.setPen(QPen(Qt::white, 2));
    const QPointF corners[4] = {d.topLeft(), d.topRight(), d.bottomRight(),
                                d.bottomLeft()};
    for (const auto& c : corners) {
      if (skin) p.drawRect(QRectF(c.x() - HANDLE, c.y() - HANDLE, 2 * HANDLE, 2 * HANDLE));
      else p.drawEllipse(c, HANDLE, HANDLE);
    }
  }
}

