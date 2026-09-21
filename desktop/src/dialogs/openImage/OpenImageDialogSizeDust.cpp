// The crop's page-size row and its Custom W×H group, each forming and falling on its own
// clock so picking Custom plays independently of the row around it.
#include "../../support/motionPrefs.hpp"
#include "../../support/theme/filterFade.hpp"
#include "OpenImageDialog.hpp"
#include "chipDust.hpp"
#include "openImageDialogParts.hpp"
#include <QGraphicsOpacityEffect>
#include <QLayout>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

namespace stencil::gui {

  // Shared by the page-size row and the Custom group: they differ only in which widget and
  // which slide clock they run on.
  void OpenImageDialog::rowChipDust(QWidget* row, QVariantAnimation*& slide, bool arriving) {
    QWidget* host = row->parentWidget();
    if (!host || support::motionReduced() || motion.quietCrop) {
      if (slide) slide->stop();
      row->setMinimumHeight(0);
      row->setMaximumHeight(QWIDGETSIZE_MAX);
      row->setVisible(arriving);
      return;
    }
    if (!arriving) {
      const QPixmap shot = row->grab();
      if (!shot.isNull()) chipCloud(host, row, shot, DisintegrateOverlay::Sweep::FALL);
      slideRowHeight(this, row, slide, false);
      return;
    }
    slideRowHeight(this, row, slide, true);
    if (QLayout* l = host->layout()) l->activate();
    const QPixmap shot = row->grab();
    auto* veil = new QGraphicsOpacityEffect(row);
    veil->setOpacity(0.0);
    row->setGraphicsEffect(veil);
    QPointer<QWidget> w(row);
    const int gen = motion.gen;
    QVariantAnimation** slidePtr = &slide;
    QTimer::singleShot(0, w, [this, w, veil, shot, gen, slidePtr] {
      const auto lift = [w, veil] {
        if (w && w->graphicsEffect() == veil) w->setGraphicsEffect(nullptr);
      };
      if (!w) return;
      if (gen != motion.gen) { lift(); return; }
      QWidget* under = w->parentWidget();
      DisintegrateOverlay* fx = (shot.isNull() || !under)
          ? nullptr : chipCloud(under, w, shot, DisintegrateOverlay::Sweep::GATHER);
      if (!fx) { lift(); return; }
      const auto follow = [fx, w, under] {
        if (w && under) fx->move(w->mapTo(under, QPoint()));
      };
      if (*slidePtr) connect(*slidePtr, &QVariantAnimation::valueChanged, fx, follow);
      if (size.anim) connect(size.anim, &QVariantAnimation::valueChanged, fx, follow);
      QTimer::singleShot(int(CHIP_DUST_MS * FILTER_DUST_VEIL_STOP), w, lift);
    });
  }

  // The page-size row's own arrival/departure — a second widget on its own clock (dust and
  // slide can be mid-flight on both rows at once).
  void OpenImageDialog::cropSizeRowDust(bool arriving) {
    if (arriving == motion.sizeRowShown) return;
    motion.sizeRowShown = arriving;
    rowChipDust(cropSizeRow, motion.sizeRowAnim, arriving);
  }

  // The Custom W×H group's own arrival/departure. Browser twin: openImageModal.js's
  // syncCropSizeCustom.
  void OpenImageDialog::cropSizeCustomDust(bool arriving) {
    if (arriving == motion.sizeCustomShown) return;
    motion.sizeCustomShown = arriving;
    rowChipDust(cropSizeCustomGroup, motion.sizeCustomAnim, arriving);
  }

}  // namespace stencil::gui
