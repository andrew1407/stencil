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

  void CropPreview::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const int corner = cornerAt(e->pos());
    dragStartImg_ = toImage(e->pos());
    dragStartRect_ = rect_;
    if (corner >= 0) {
      drag_ = Drag::RESIZE;
      dragCorner_ = corner;
    } else if (displayRect().contains(e->pos())) {
      drag_ = Drag::MOVE;
    } else {
      drag_ = Drag::NONE;
    }
  }

  void CropPreview::mouseMoveEvent(QMouseEvent* e) {
    // Cursor feedback even when not dragging.
    if (drag_ == Drag::NONE) {
      const int c = cornerAt(e->pos());
      if (c == 0 || c == 2) setCursor(Qt::SizeFDiagCursor);
      else if (c == 1 || c == 3) setCursor(Qt::SizeBDiagCursor);
      else if (displayRect().contains(e->pos())) setCursor(Qt::SizeAllCursor);
      else unsetCursor();
      return;
    }
    const core::Point cur = toImage(e->pos());
    if (drag_ == Drag::MOVE) {
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
    drag_ = Drag::NONE;
    dragCorner_ = -1;
  }

  // Mouse wheel over the crop rect grows/shrinks it about its centre (aspect locked, clamped) via core::scaleCropCentered — mirrors the browser.
  void CropPreview::wheelEvent(QWheelEvent* e) {
    const double dy = e->angleDelta().y();
    const QPoint pos = e->position().toPoint();
    if (iw_ <= 0 || dy == 0.0 || !displayRect().contains(pos)) { e->ignore(); return; }
    rect_ = core::scaleCropCentered(rect_, std::pow(1.0015, dy), aspect_, iw_, ih_);
    // Re-anchor an in-progress move/resize drag so the next mouse-move doesn't snap the size back.
    if (drag_ != Drag::NONE) { dragStartRect_ = rect_; dragStartImg_ = toImage(pos); }
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
          if (drag_ != Drag::NONE) { dragStartRect_ = rect_; dragStartImg_ = toImage(pos); }
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

  // The browser shell is `width:auto` here: the preview sets the width and the footer its
  // floor (one line, so the hint sits beside the buttons). Never under MIN_DIALOG_W, never
  // narrower than the preview, never past the screen — the chrome is measured and the
  // preview re-fitted to what is left.
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
    const QSize room(avail.width() - 2 * SCREEN_MARGIN - chrome_.width(),
                     avail.height() - 2 * SCREEN_MARGIN - chrome_.height());
    if (box.width() > room.width() || box.height() > room.height()) {
      preview_->setFitBox(QSize(qMin(box.width(), room.width()), qMin(box.height(), room.height())));
      measure();
    }
    const int minW = std::max({MIN_DIALOG_W, minimumSizeHint().width(),
                               modalFooterLineWidth(chrome, footer)});
    setMinimumWidth(qMin(minW, avail.width() - 2 * SCREEN_MARGIN));
    const int w = qMax(minimumWidth(), sizeHint().width());
    const int h = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(w)
                                                : sizeHint().height();
    resize(w, qMin(h, avail.height() - 2 * SCREEN_MARGIN));
  }

  core::CropRect CropDialog::cropRect() const { return preview_->cropRect(); }
}

