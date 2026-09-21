#include "CropDialog.hpp"
#include "../../support/icon/iconSpin.hpp"
#include "cropDialogParts.hpp"
#include "../../support/modal/modalChrome.hpp"
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
    settleRect();   // a grab mid-flight takes the box where it is headed
    const int corner = cornerAt(e->pos());
    dragStartImg = toImage(e->pos());
    dragStartRect = cropBox;
    if (corner >= 0) {
      drag = Drag::RESIZE;
      dragCorner = corner;
    } else if (displayRect().contains(e->pos())) {
      drag = Drag::MOVE;
    } else {
      drag = Drag::NONE;
    }
  }

  void CropPreview::mouseMoveEvent(QMouseEvent* e) {
    // Cursor feedback even when not dragging.
    if (drag == Drag::NONE) {
      const int c = cornerAt(e->pos());
      if (c == 0 || c == 2) setCursor(Qt::SizeFDiagCursor);
      else if (c == 1 || c == 3) setCursor(Qt::SizeBDiagCursor);
      else if (displayRect().contains(e->pos())) setCursor(Qt::SizeAllCursor);
      else unsetCursor();
      return;
    }
    const core::Point cur = toImage(e->pos());
    if (drag == Drag::MOVE) {
      cropBox = core::moveCropClamped(dragStartRect, cur.x - dragStartImg.x,
                                    cur.y - dragStartImg.y, iw, ih);
    } else {
      cropBox = core::resizeCropFromCorner(dragStartRect, dragCorner, cur.x, cur.y,
                                         aspect, iw, ih);
    }
    update();
    emit cropChanged();
  }

  void CropPreview::mouseReleaseEvent(QMouseEvent*) {
    drag = Drag::NONE;
    dragCorner = -1;
  }

  // Mouse wheel over the crop rect grows/shrinks it about its centre (aspect locked, clamped) via core::scaleCropCentered — mirrors the browser.
  void CropPreview::wheelEvent(QWheelEvent* e) {
    const double dy = e->angleDelta().y();
    const QPoint pos = e->position().toPoint();
    if (iw <= 0 || dy == 0.0 || !displayRect().contains(pos)) { e->ignore(); return; }
    settleRect();
    cropBox = core::scaleCropCentered(cropBox, std::pow(1.0015, dy), aspect, iw, ih);
    // Re-anchor an in-progress move/resize drag so the next mouse-move doesn't snap the size back.
    if (drag != Drag::NONE) { dragStartRect = cropBox; dragStartImg = toImage(pos); }
    update();
    emit cropChanged();
    e->accept();
  }

  // Trackpad pinch (native ZOOM gesture) scales the crop from its centre when the cursor is inside — mirrors the browser ctrl+wheel pinch.
  bool CropPreview::event(QEvent* e) {
    if (e->type() == QEvent::NativeGesture) {
      auto* g = static_cast<QNativeGestureEvent*>(e);
      if (g->gestureType() == Qt::ZoomNativeGesture && iw > 0) {
        const QPoint pos = g->position().toPoint();
        if (displayRect().contains(pos)) {
          settleRect();
          cropBox = core::scaleCropCentered(cropBox, 1.0 + g->value(), aspect, iw, ih);
          if (drag != Drag::NONE) { dragStartRect = cropBox; dragStartImg = toImage(pos); }
          update();
          emit cropChanged();
          return true;
        }
      }
    }
    return QWidget::event(e);
  }

  // The browser's crop modal on the shared shell (modalChrome): crop glyph + "Crop Image", the
  // preview and its size line centred, footer of hint / Album-Portrait / Cancel / Apply Crop.
  CropDialog::CropDialog(const QImage& original, double pageWidthCm,
                         double pageHeightCm, bool album,
                         const core::CropRect& initial, QWidget* parent)
      : QDialog(parent) {
    setWindowTitle(tr("Crop Image"));
    ModalChrome chrome = installModalChrome(this, QStringLiteral("crop"), tr("Crop Image"));
    chrome.body->setSpacing(12);   // browser .settings-body gap: 12px

    preview = new CropPreview(original, pageWidthCm, pageHeightCm, initial, this);
    if (initial.width <= 0) preview->setAlbum(album);
    chrome.body->addWidget(preview, 0, Qt::AlignHCenter);

    auto* dims = new QLabel(this);
    dims->setObjectName(QStringLiteral("cropDims"));
    dims->setAlignment(Qt::AlignCenter);
    chrome.body->addWidget(dims);

    QHBoxLayout* footer = addModalFooter(
        chrome, tr("Drag to move · drag a corner to resize (aspect locked to the page)."));
    // Browser order: the hint leads, then the orientation toggle with the other buttons
    // (#crop-orientation) — text left, every button right.
    orientationBtn = new QPushButton(this);
    makeModalCta(orientationBtn, QStringLiteral("swap"));
    orientationBtn->setToolTip(tr("Swap album / portrait — flips the crop orientation"));
    orientationBtn->setAutoDefault(false);
    footer->addWidget(orientationBtn);
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
      const core::CropRect r = preview->cropRect();
      dims->setText(QString("%1 × %2 px · %3")
                        .arg(qRound(r.width))
                        .arg(qRound(r.height))
                        .arg(preview->getAlbum() ? tr("Album (landscape)") : tr("Portrait")));
      orientationBtn->setText(preview->getAlbum() ? tr("Album") : tr("Portrait"));
    };
    connect(preview, &CropPreview::cropChanged, this, refresh);
    connect(orientationBtn, &QPushButton::clicked, this, [this] {
      preview->setAlbum(!preview->getAlbum());
      support::spinIconOnce(orientationBtn);   // the press turns the glyph it flips
    });
    refresh();
    fitToScreen(chrome, footer);
  }

  // The browser shell is `width:auto` here: the preview sets the width and the footer its floor.
  // Never under MIN_DIALOG_W, never narrower than the preview, never past the screen.
  void CropDialog::fitToScreen(const ModalChrome& chrome, const QHBoxLayout* footer) {
    const QRect avail = screenAvail(this);
    const QSize box = previewFitBox(avail);
    // A hidden widget's size change never reaches the layouts' caches (updateGeometry stops at a
    // hidden widget), and the shell's layout tree is a widget away, so measurements refresh by hand.
    const auto measure = [this] {
      for (QWidget* w : findChildren<QWidget*>()) w->updateGeometry();   // the items' caches
      for (QLayout* l : findChildren<QLayout*>()) l->invalidate();        // the boxes'
      layout()->invalidate();
      layout()->activate();
    };
    preview->setFitBox(box);
    measure();
    const QSize chromeSize(minimumSizeHint().width() - preview->width(),
                        sizeHint().height() - preview->height());
    const QSize room(avail.width() - 2 * SCREEN_MARGIN - chromeSize.width(),
                     avail.height() - 2 * SCREEN_MARGIN - chromeSize.height());
    if (box.width() > room.width() || box.height() > room.height()) {
      preview->setFitBox(QSize(qMin(box.width(), room.width()), qMin(box.height(), room.height())));
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

  core::CropRect CropDialog::cropRect() const { return preview->cropRect(); }
}

