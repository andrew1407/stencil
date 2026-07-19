#include "cropDialog.hpp"
#include "guiHelpers.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace stencil::gui {

  namespace {
    constexpr int kHandle = 7;     // handle radius (display px)
    constexpr int kMaxDispW = 760;  // preview fit box
    constexpr int kMaxDispH = 540;
  }  // namespace

  // ── CropPreview ──────────────────────────────────────────────────────────
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

    // Fit the original into the preview box (allow modest upscaling of small
    // images so the handles are usable).
    const double s = std::min(static_cast<double>(kMaxDispW) / std::max(1, iw_),
                              static_cast<double>(kMaxDispH) / std::max(1, ih_));
    scale_ = s > 0 ? s : 1.0;
    setFixedSize(qRound(iw_ * scale_), qRound(ih_ * scale_));
    setMouseTracking(true);
  }

  void CropPreview::setAlbum(bool album) {
    album_ = album;
    aspect_ = core::cropAspect(pageWidthCm_, pageHeightCm_, album_);
    rect_ = core::centeredCrop(iw_, ih_, aspect_);
    update();
    emit cropChanged();
  }

  core::Point CropPreview::toImage(const QPoint& w) const {
    return {w.x() / scale_, w.y() / scale_};
  }

  QRectF CropPreview::displayRect() const {
    return QRectF(rect_.x * scale_, rect_.y * scale_, rect_.width * scale_,
                  rect_.height * scale_);
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
    p.drawImage(rect(), original_);

    const QRectF d = displayRect();
    // Dim everything outside the crop (even-odd fill of full rect minus crop).
    QPainterPath outside;
    outside.setFillRule(Qt::OddEvenFill);
    outside.addRect(QRectF(rect()));
    outside.addRect(d);
    p.fillPath(outside, QColor(0, 0, 0, 115));

    QPen pen(QColor("#4da3ff"));
    pen.setWidth(2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(d);

    p.setBrush(QColor("#4da3ff"));
    p.setPen(QPen(Qt::white, 2));
    const QPointF corners[4] = {d.topLeft(), d.topRight(), d.bottomRight(),
                                d.bottomLeft()};
    for (const auto& c : corners) p.drawEllipse(c, kHandle, kHandle);
  }

  void CropPreview::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const int corner = cornerAt(e->pos());
    dragStartImg_ = toImage(e->pos());
    dragStartRect_ = rect_;
    if (corner >= 0) {
      drag_ = Drag::Resize;
      dragCorner_ = corner;
    } else if (displayRect().contains(e->pos())) {
      drag_ = Drag::Move;
    } else {
      drag_ = Drag::None;
    }
  }

  void CropPreview::mouseMoveEvent(QMouseEvent* e) {
    // Cursor feedback even when not dragging.
    if (drag_ == Drag::None) {
      const int c = cornerAt(e->pos());
      if (c == 0 || c == 2) setCursor(Qt::SizeFDiagCursor);
      else if (c == 1 || c == 3) setCursor(Qt::SizeBDiagCursor);
      else if (displayRect().contains(e->pos())) setCursor(Qt::SizeAllCursor);
      else unsetCursor();
      return;
    }
    const core::Point cur = toImage(e->pos());
    if (drag_ == Drag::Move) {
      rect_ = core::moveCropClamped(dragStartRect_, cur.x - dragStartImg_.x,
                                    cur.y - dragStartImg_.y, iw_, ih_);
    } else {
      rect_ = core::resizeCropFromCorner(dragStartRect_, dragCorner_, cur.x, cur.y,
                                         aspect_, iw_, ih_);
    }
    update();
    emit cropChanged();
  }

  void CropPreview::mouseReleaseEvent(QMouseEvent*) {
    drag_ = Drag::None;
    dragCorner_ = -1;
  }

  // Mouse wheel over the crop rect grows/shrinks it about its centre (aspect locked, clamped) via core::scaleCropCentered — mirrors the browser.
  void CropPreview::wheelEvent(QWheelEvent* e) {
    const double dy = e->angleDelta().y();
    const QPoint pos = e->position().toPoint();
    if (iw_ <= 0 || dy == 0.0 || !displayRect().contains(pos)) { e->ignore(); return; }
    rect_ = core::scaleCropCentered(rect_, std::pow(1.0015, dy), aspect_, iw_, ih_);
    // Re-anchor an in-progress move/resize drag so the next mouse-move doesn't snap the size back.
    if (drag_ != Drag::None) { dragStartRect_ = rect_; dragStartImg_ = toImage(pos); }
    update();
    emit cropChanged();
    e->accept();
  }

  // Trackpad pinch (native ZOOM gesture) scales the crop from its centre when the cursor is inside — mirrors the browser ctrl+wheel pinch.
  bool CropPreview::event(QEvent* e) {
    if (e->type() == QEvent::NativeGesture) {
      auto* g = static_cast<QNativeGestureEvent*>(e);
      if (g->gestureType() == Qt::ZoomNativeGesture && iw_ > 0) {
        const QPoint pos = g->position().toPoint();
        if (displayRect().contains(pos)) {
          rect_ = core::scaleCropCentered(rect_, 1.0 + g->value(), aspect_, iw_, ih_);
          if (drag_ != Drag::None) { dragStartRect_ = rect_; dragStartImg_ = toImage(pos); }
          update();
          emit cropChanged();
          return true;
        }
      }
    }
    return QWidget::event(e);
  }

  // ── CropDialog ───────────────────────────────────────────────────────────
  CropDialog::CropDialog(const QImage& original, double pageWidthCm,
                         double pageHeightCm, bool album,
                         const core::CropRect& initial, QWidget* parent)
      : QDialog(parent) {
    setWindowTitle("Crop Image");

    auto* layout = new QVBoxLayout(this);
    preview_ = new CropPreview(original, pageWidthCm, pageHeightCm, initial, this);
    if (initial.width <= 0) preview_->setAlbum(album);

    auto* previewRow = new QHBoxLayout;
    previewRow->addStretch(1);
    previewRow->addWidget(preview_);
    previewRow->addStretch(1);
    layout->addLayout(previewRow);

    auto* dims = new QLabel(this);
    dims->setAlignment(Qt::AlignCenter);
    dims->setStyleSheet("color: gray; font-size: 12px;");
    layout->addWidget(dims);

    auto* controls = new QHBoxLayout;
    orientationBtn_ = new QPushButton(this);
    orientationBtn_->setToolTip(
        "Swap album / portrait — flips the crop orientation");
    controls->addWidget(orientationBtn_);
    controls->addStretch(1);
    auto* hint = new QLabel(
        "Drag to move · drag a corner to resize (aspect locked to the page).",
        this);
    hint->setStyleSheet("color: gray; font-size: 11px;");
    controls->addWidget(hint);
    layout->addLayout(controls);

    auto* box = makeButtonBox(this, QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText("Apply Crop");
    layout->addWidget(box);

    auto refresh = [this, dims] {
      const core::CropRect r = preview_->cropRect();
      dims->setText(QString("%1 × %2 px · %3")
                        .arg(qRound(r.width))
                        .arg(qRound(r.height))
                        .arg(preview_->album() ? "Album (landscape)" : "Portrait"));
      orientationBtn_->setText(preview_->album() ? "⤢ Album" : "⤡ Portrait");
    };
    connect(preview_, &CropPreview::cropChanged, this, refresh);
    connect(orientationBtn_, &QPushButton::clicked, this,
            [this] { preview_->setAlbum(!preview_->album()); });
    refresh();
  }

  core::CropRect CropDialog::cropRect() const { return preview_->cropRect(); }

}
