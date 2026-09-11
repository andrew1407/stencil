#include "cropDialog.hpp"
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

  namespace {
    // Browser crop-handle: a 14px accent disc inside a 2px white ring (cropModal.js
    // .crop-handle) — drawn as an r=8 ellipse under a 2px pen, so the ring's outer
    // edge lands at r=9 and the disc keeps its 14px.
    constexpr int kHandle = 8;
    // The image sits this far inside the widget, so a handle on the image edge draws
    // whole instead of being sliced in half (browser: handles live in the UNclipped stage).
    constexpr int kInset = kHandle + 2;
    const QColor kCropAccent(0x4d, 0xa3, 0xff);   // #4da3ff, the browser's crop blue
    constexpr int kShadeAlpha = 115;                   // rgba(0,0,0,0.45)
    constexpr int kMinDispW = 760;  // the preview fit box never shrinks below this…
    constexpr int kMinDispH = 540;
    constexpr int kMinDialogW = 640;   // the dialog's own floor
    constexpr int kScreenMargin = 20;  // …but the window always keeps this much screen around it

    // The screen the dialog lands on — its parent window's (a dialog centres over its
    // parent), else the primary — as LOGICAL px: availableGeometry is device-independent
    // on every platform, and leaves out the menu bar / dock / taskbar.
    QRect screenAvail(const QWidget* w) {
      const QWidget* top = w ? w->window() : nullptr;
      if (top && top->parentWidget()) top = top->parentWidget()->window();
      const QScreen* screen = top ? top->screen() : nullptr;
      if (!screen) screen = QGuiApplication::primaryScreen();
      return screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
    }

    // The browser's preview box (cropModal.js #crop-image-el: max-width calc(96vw - 60px),
    // max-height calc(82vh - 180px)) taken of that screen — never below 760×540, so a
    // small screen keeps a usable box (fitToScreen still caps the window itself).
    QSize previewFitBox(const QRect& avail) {
      return QSize(qMax(kMinDispW, qRound(avail.width() * 0.96) - 60),
                   qMax(kMinDispH, qRound(avail.height() * 0.82) - 180));
    }
  }  // namespace

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

  // The browser's crop modal on the shared shell (modalChrome): crop glyph + "Crop
  // Image" over the hairline, the preview and its size line centred in the body, and
  // a footer of hint · Album/Portrait · Cancel · Apply Crop, every button an accent CTA.
  CropDialog::CropDialog(const QImage& original, double pageWidthCm,
                         double pageHeightCm, bool album,
                         const core::CropRect& initial, QWidget* parent)
      : QDialog(parent) {
    setWindowTitle(tr("Crop Image"));
    ModalChrome chrome = installModalChrome(this, QStringLiteral("crop"), tr("Crop Image"));
    chrome.body->setSpacing(12);   // browser .settings-body gap: 12px

    preview_ = new CropPreview(original, pageWidthCm, pageHeightCm, initial, this);
    if (initial.width <= 0) preview_->setAlbum(album);
    chrome.body->addWidget(preview_, 0, Qt::AlignHCenter);

    auto* dims = new QLabel(this);
    dims->setObjectName(QStringLiteral("cropDims"));
    dims->setAlignment(Qt::AlignCenter);
    chrome.body->addWidget(dims);

    QHBoxLayout* footer = addModalFooter(
        chrome, tr("Drag to move · drag a corner to resize (aspect locked to the page)."));
    // Browser order: the hint leads, then the orientation toggle with the other buttons
    // (#crop-orientation) — text left, every button right.
    orientationBtn_ = new QPushButton(this);
    makeModalCta(orientationBtn_, QStringLiteral("swap"));
    orientationBtn_->setToolTip(tr("Swap album / portrait — flips the crop orientation"));
    orientationBtn_->setAutoDefault(false);
    footer->addWidget(orientationBtn_);
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    makeModalCta(cancelBtn, QStringLiteral("x"));
    cancelBtn->setAutoDefault(false);
    footer->addWidget(cancelBtn);
    auto* applyBtn = new QPushButton(tr("Apply Crop"), this);
    makeModalCta(applyBtn, QStringLiteral("check"));
    applyBtn->setDefault(true);   // Enter applies, Escape rejects (QDialog)
    applyBtn->setAutoDefault(true);
    footer->addWidget(applyBtn);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto refresh = [this, dims] {
      const core::CropRect r = preview_->cropRect();
      dims->setText(QString("%1 × %2 px · %3")
                        .arg(qRound(r.width))
                        .arg(qRound(r.height))
                        .arg(preview_->album() ? tr("Album (landscape)") : tr("Portrait")));
      orientationBtn_->setText(preview_->album() ? tr("Album") : tr("Portrait"));
    };
    connect(preview_, &CropPreview::cropChanged, this, refresh);
    connect(orientationBtn_, &QPushButton::clicked, this,
            [this] { preview_->setAlbum(!preview_->album()); });
    refresh();
    fitToScreen(chrome, footer);
  }

  // The browser shell is `width:auto` here: the preview sets the width, and the footer
  // its floor — the modal is as wide as its footer's ONE line, so the hint opens beside
  // the buttons rather than wrapped above them and the preview centres in the wider
  // body. Never under kMinDialogW, never narrower than the preview itself — and never
  // past the screen: the chrome around the preview (header, size line, footer, padding)
  // is measured and the preview re-fitted to what the screen leaves for it.
  void CropDialog::fitToScreen(const ModalChrome& chrome, const QHBoxLayout* footer) {
    const QRect avail = screenAvail(this);
    const QSize box = previewFitBox(avail);
    // A hidden widget's size change never reaches the layouts' caches (updateGeometry
    // stops at a hidden widget), and the shell's layout tree is a widget away from the
    // dialog's own, so every measurement below refreshes the lot by hand.
    const auto measure = [this] {
      for (QWidget* w : findChildren<QWidget*>()) w->updateGeometry();   // the items' caches
      for (QLayout* l : findChildren<QLayout*>()) l->invalidate();        // the boxes'
      layout()->invalidate();
      layout()->activate();
    };
    preview_->setFitBox(box);
    measure();
    const QSize chrome_(minimumSizeHint().width() - preview_->width(),
                        sizeHint().height() - preview_->height());
    const QSize room(avail.width() - 2 * kScreenMargin - chrome_.width(),
                     avail.height() - 2 * kScreenMargin - chrome_.height());
    if (box.width() > room.width() || box.height() > room.height()) {
      preview_->setFitBox(QSize(qMin(box.width(), room.width()), qMin(box.height(), room.height())));
      measure();
    }
    const int minW = std::max({kMinDialogW, minimumSizeHint().width(),
                               modalFooterLineWidth(chrome, footer)});
    setMinimumWidth(qMin(minW, avail.width() - 2 * kScreenMargin));
    const int w = qMax(minimumWidth(), sizeHint().width());
    const int h = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(w)
                                                : sizeHint().height();
    resize(w, qMin(h, avail.height() - 2 * kScreenMargin));
  }

  core::CropRect CropDialog::cropRect() const { return preview_->cropRect(); }

}
