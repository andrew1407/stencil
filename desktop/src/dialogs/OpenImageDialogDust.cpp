#include "../support/DisintegrateOverlay.hpp"
#include "../support/filterFade.hpp"
#include "../support/motionPrefs.hpp"
#include "OpenImageDialog.hpp"
#include "chipDust.hpp"
#include "openImageDialogParts.hpp"
#include <algorithm>
#include <QDateTime>
#include <QPainter>
#include <cmath>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLayout>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

namespace stencil::gui {

  // The keyword chip's mesh and clock (KeywordChipsMotion.cpp; browser motion/tiles.js).

  // The preview's own ARRIVAL (browser ghostIn parity): the label stays veiled while an overlay
  // assembles it. With motion off the overlay no-ops, so veiling would just hide the picture.
  void OpenImageDialog::gatherPreviewDust(const QPixmap& shot) {
    QWidget* host = previewLabel_->parentWidget();
    if (!host || shot.isNull() || support::motionReduced()) return;
    auto* veil = new QGraphicsOpacityEffect(previewLabel_);
    veil->setOpacity(0.0);
    previewLabel_->setGraphicsEffect(veil);
    QPointer<QLabel> label(previewLabel_);
    const int gen = motion_.gen;
    // After the height ease has LANDED — a cloud pinned mid-resize flies to where the label
    // was passing through — and after any departing picture has fallen: two clouds read as noise.
    const qint64 left = motion_.scatterEnds - QDateTime::currentMSecsSinceEpoch();
    QTimer::singleShot(std::max<qint64>(OI_RESIZE_MS, left), label,
                       [this, label, shot, veil, gen] {
      if (!label || gen != motion_.gen) return;   // the tab moved on while this waited
      if (QLayout* l = label->parentWidget()->layout()) l->activate();
      auto* cloud = DisintegrateOverlay::overRect(label, label->rect(), label->parentWidget(),
                                                  DisintegrateOverlay::Sweep::GATHER, false,
                                                  DisintegrateOverlay::DUST_MAX_CELLS,
                                                  CANVAS_DUST_MS, QColor(), shot);
      const auto lift = [label, veil] {
        if (label && label->graphicsEffect() == veil) label->setGraphicsEffect(nullptr);
      };
      if (cloud) connect(cloud, &QObject::destroyed, label, lift);
      else lift();
    });
  }

  // Arrives and leaves on the KEYWORD-CHIP recipe (KeywordChipsMotion; the browser read-out flies
  // the same). Photographed before the veil (a veiled widget grabs to nothing) and once shown.
  void OpenImageDialog::cropDimsDust(bool arriving) {
    QWidget* host = cropDims_->parentWidget();
    // isVisible() cannot answer this any more: the line stays shown while it slides away.
    if (arriving == motion_.dimsShown) return;   // a re-sync is not a state change
    motion_.dimsShown = arriving;
    if (!host || support::motionReduced() || motion_.quietCrop) {
      if (motion_.dimsAnim) motion_.dimsAnim->stop();
      cropDims_->setMinimumHeight(0);
      cropDims_->setMaximumHeight(QWIDGETSIZE_MAX);
      cropDims_->setVisible(arriving);
      return;
    }
    // The mesh comes from the BOX, as the keyword chip's and the browser's do (dustGrid is
    // motion/tiles.js reshapeGrid): a fixed 64x22 budget over a 13px line flew as horizontal slices.
    if (!arriving) {
      const QPixmap shot = cropDims_->grab();   // before it goes: see below
      if (!shot.isNull()) chipCloud(host, cropDims_, shot, DisintegrateOverlay::Sweep::FALL);
      slideRowHeight(this, cropDims_, motion_.dimsAnim, false);
      return;
    }
    slideRowHeight(this, cropDims_, motion_.dimsAnim, true);
    if (QLayout* l = host->layout()) l->activate();
    const QPixmap shot = cropDims_->grab();
    auto* veil = new QGraphicsOpacityEffect(cropDims_);
    veil->setOpacity(0.0);
    cropDims_->setGraphicsEffect(veil);
    QPointer<QLabel> label(cropDims_);
    const int gen = motion_.gen;
    // ONE turn later, not synchronously: the resize syncCropStage just asked for lands when this turn
    // ends, so a cloud raised now is pinned where the line USED to be. The veil lifts early.
    QTimer::singleShot(0, label, [this, label, veil, shot, gen, host] {
      const auto lift = [label, veil] {
        if (label && label->graphicsEffect() == veil) label->setGraphicsEffect(nullptr);
      };
      if (!label) return;
      if (gen != motion_.gen) { lift(); return; }
      DisintegrateOverlay* fx = shot.isNull()
          ? nullptr : chipCloud(host, label, shot, DisintegrateOverlay::Sweep::GATHER);
      if (!fx) {
        lift();
        return;
      }
      // The line it is assembling is still MOVING - the column slides it and the first cropChanged
      // re-words it - so the layer follows it (browser twin: retargetDust).
      QWidget* under = label->parentWidget();
      const auto follow = [fx, label, under] {
        if (label && under) fx->move(label->mapTo(under, QPoint()));
      };
      if (motion_.dimsAnim) connect(motion_.dimsAnim, &QVariantAnimation::valueChanged, fx, follow);
      if (size_.anim) connect(size_.anim, &QVariantAnimation::valueChanged, fx, follow);
      QTimer::singleShot(int(CHIP_DUST_MS * FILTER_DUST_VEIL_STOP), label, lift);
    });
  }

  // The line SLIDES in and out on the window's own clock (browser twin: syncCropDims). The END state
  // goes into the layout FIRST: the refit runs next and must measure the shape this lands on.
  void OpenImageDialog::scatterPreviewDust() {
    QWidget* host = previewLabel_->parentWidget();
    if (!previewLabel_->isVisible() || previewLabel_->pixmap().isNull()) return;
    const QPixmap shot = previewLabel_->grab(previewLabel_->rect());
    const QRect box = previewLabel_->rect();
    // Raised while the label is STILL SHOWN (overRect refuses an invisible source); the room
    // it took goes straight after, the cloud carrying the picture on.
    if (host && !shot.isNull() && !support::motionReduced()) {
      motion_.scatterEnds = QDateTime::currentMSecsSinceEpoch() + CANVAS_DUST_MS;
      motion_.arrivalDue = true;   // whatever lands next flies in, seen before or not
      DisintegrateOverlay::overRect(previewLabel_, box, host, DisintegrateOverlay::Sweep::FALL,
                                    false, DisintegrateOverlay::DUST_MAX_CELLS, CANVAS_DUST_MS,
                                    QColor(), shot);
    }
    clearPreviewImage();
  }

  // A cloud is a child of the column, not of the picture it came from, so a tab switch left motes
  // playing over whatever arrived next. Every veil a cloud stood in for lifts with it.
  void OpenImageDialog::cancelPreviewDust() {
    if (!previewLabel_) return;
    ++motion_.gen;   // a raise still queued behind a timer is now stale
    for (QWidget* w : findChildren<QWidget*>(
             QString::fromLatin1(DisintegrateOverlay::OBJECT_NAME)))
      delete w;
    previewLabel_->setGraphicsEffect(nullptr);
    if (cropDims_) cropDims_->setGraphicsEffect(nullptr);
    if (cropSizeRow_) cropSizeRow_->setGraphicsEffect(nullptr);
    if (cropAlbum_) cropAlbum_->setGraphicsEffect(nullptr);
    if (cropSizeCustomGroup_) cropSizeCustomGroup_->setGraphicsEffect(nullptr);
    motion_.scatterEnds = 0;
  }

  // reject() hides the dialog while the popover's own collapse still runs, so an
  // unfinished cloud played on over the closing panel.
  void OpenImageDialog::hideEvent(QHideEvent* event) {
    cancelPreviewDust();
    QDialog::hideEvent(event);
  }

}  // namespace stencil::gui
