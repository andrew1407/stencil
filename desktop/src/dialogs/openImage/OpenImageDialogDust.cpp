#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/theme/filterFade.hpp"
#include "../../support/motionPrefs.hpp"
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
    QWidget* host = previewLabel->parentWidget();
    if (!host || shot.isNull() || support::motionReduced()) return;
    auto* veil = new QGraphicsOpacityEffect(previewLabel);
    veil->setOpacity(0.0);
    previewLabel->setGraphicsEffect(veil);
    QPointer<QLabel> label(previewLabel);
    const int gen = motion.gen;
    // After the height ease has LANDED — a cloud pinned mid-resize flies to where the label
    // was passing through — and after any departing picture has fallen: two clouds read as noise.
    const qint64 left = motion.scatterEnds - QDateTime::currentMSecsSinceEpoch();
    QTimer::singleShot(std::max<qint64>(OI_RESIZE_MS, left), label,
                       [this, label, shot, veil, gen] {
      if (!label || gen != motion.gen) return;   // the tab moved on while this waited
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
    QWidget* host = cropDims->parentWidget();
    // isVisible() cannot answer this any more: the line stays shown while it slides away.
    if (arriving == motion.dimsShown) return;   // a re-sync is not a state change
    motion.dimsShown = arriving;
    if (!host || support::motionReduced() || motion.quietCrop) {
      if (motion.dimsAnim) motion.dimsAnim->stop();
      cropDims->setMinimumHeight(0);
      cropDims->setMaximumHeight(QWIDGETSIZE_MAX);
      cropDims->setVisible(arriving);
      return;
    }
    // The mesh comes from the BOX, as the keyword chip's and the browser's do (dustGrid is
    // motion/tiles.js reshapeGrid): a fixed 64x22 budget over a 13px line flew as horizontal slices.
    if (!arriving) {
      const QPixmap shot = cropDims->grab();   // before it goes: see below
      if (!shot.isNull()) chipCloud(host, cropDims, shot, DisintegrateOverlay::Sweep::FALL);
      slideRowHeight(this, cropDims, motion.dimsAnim, false);
      return;
    }
    slideRowHeight(this, cropDims, motion.dimsAnim, true);
    if (QLayout* l = host->layout()) l->activate();
    const QPixmap shot = cropDims->grab();
    auto* veil = new QGraphicsOpacityEffect(cropDims);
    veil->setOpacity(0.0);
    cropDims->setGraphicsEffect(veil);
    QPointer<QLabel> label(cropDims);
    const int gen = motion.gen;
    // ONE turn later, not synchronously: the resize syncCropStage just asked for lands when this turn
    // ends, so a cloud raised now is pinned where the line USED to be. The veil lifts early.
    QTimer::singleShot(0, label, [this, label, veil, shot, gen, host] {
      const auto lift = [label, veil] {
        if (label && label->graphicsEffect() == veil) label->setGraphicsEffect(nullptr);
      };
      if (!label) return;
      if (gen != motion.gen) { lift(); return; }
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
      if (motion.dimsAnim) connect(motion.dimsAnim, &QVariantAnimation::valueChanged, fx, follow);
      if (size.anim) connect(size.anim, &QVariantAnimation::valueChanged, fx, follow);
      QTimer::singleShot(int(CHIP_DUST_MS * FILTER_DUST_VEIL_STOP), label, lift);
    });
  }

  // The line SLIDES in and out on the window's own clock (browser twin: syncCropDims). The END state
  // goes into the layout FIRST: the refit runs next and must measure the shape this lands on.
  void OpenImageDialog::scatterPreviewDust() {
    QWidget* host = previewLabel->parentWidget();
    if (!previewLabel->isVisible() || previewLabel->pixmap().isNull()) return;
    const QPixmap shot = previewLabel->grab(previewLabel->rect());
    const QRect box = previewLabel->rect();
    // Raised while the label is STILL SHOWN (overRect refuses an invisible source); the room
    // it took goes straight after, the cloud carrying the picture on.
    if (host && !shot.isNull() && !support::motionReduced()) {
      motion.scatterEnds = QDateTime::currentMSecsSinceEpoch() + CANVAS_DUST_MS;
      motion.arrivalDue = true;   // whatever lands next flies in, seen before or not
      DisintegrateOverlay::overRect(previewLabel, box, host, DisintegrateOverlay::Sweep::FALL,
                                    false, DisintegrateOverlay::DUST_MAX_CELLS, CANVAS_DUST_MS,
                                    QColor(), shot);
    }
    clearPreviewImage();
  }

  // A cloud is a child of the column, not of the picture it came from, so a tab switch left motes
  // playing over whatever arrived next. Every veil a cloud stood in for lifts with it.
  void OpenImageDialog::cancelPreviewDust() {
    if (!previewLabel) return;
    ++motion.gen;   // a raise still queued behind a timer is now stale
    for (QWidget* w : findChildren<QWidget*>(
             QString::fromLatin1(DisintegrateOverlay::OBJECT_NAME)))
      delete w;
    previewLabel->setGraphicsEffect(nullptr);
    if (cropDims) cropDims->setGraphicsEffect(nullptr);
    if (cropSizeRow) cropSizeRow->setGraphicsEffect(nullptr);
    if (cropAlbum) cropAlbum->setGraphicsEffect(nullptr);
    if (cropSizeCustomGroup) cropSizeCustomGroup->setGraphicsEffect(nullptr);
    motion.scatterEnds = 0;
  }

  // reject() hides the dialog while the popover's own collapse still runs, so an
  // unfinished cloud played on over the closing panel.
  void OpenImageDialog::hideEvent(QHideEvent* event) {
    cancelPreviewDust();
    QDialog::hideEvent(event);
  }

}  // namespace stencil::gui
