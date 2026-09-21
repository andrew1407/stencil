// The Album/Portrait toggle's own materialize and fall: inline beside the always-shown
// checkbox, so unlike the rows beside it it has no height of its own to slide.
#include "../support/motionPrefs.hpp"
#include "../support/filterFade.hpp"
#include "OpenImageDialog.hpp"
#include "chipDust.hpp"
#include <QGraphicsOpacityEffect>
#include <QLayout>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

namespace stencil::gui {

  // The button falls INLINE beside the always-shown checkbox, so it has no height to slide - just
  // the chip cloud. Both directions FOLLOW size.anim, or the cloud is stranded over the stage.
  void OpenImageDialog::cropAlbumDust(bool arriving) {
    if (arriving == motion.albumShown) return;
    motion.albumShown = arriving;
    QWidget* host = cropAlbum->parentWidget();
    if (!host || support::motionReduced() || motion.quietCrop) {
      cropAlbum->setVisible(arriving);
      return;
    }
    QPointer<QPushButton> btn(cropAlbum);
    const int gen = motion.gen;
    if (!arriving) {
      // SYNCHRONOUS, like cropDims's own pin(0): syncCropStage() measures the wanted height in THIS
      // SAME call, and a button still occupying the row's width read as "still here".
      const QPixmap shot = cropAlbum->grab();   // before it goes
      cropAlbum->setVisible(false);
      if (!shot.isNull()) {
        DisintegrateOverlay* fx = chipCloud(host, cropAlbum, shot, DisintegrateOverlay::Sweep::FALL);
        if (fx && size.anim) {
          const auto follow = [fx, btn, host] { if (btn && host) fx->move(btn->mapTo(host, QPoint())); };
          connect(size.anim, &QVariantAnimation::valueChanged, fx, follow);
        }
      }
      return;
    }
    cropAlbum->setVisible(true);
    if (QLayout* l = host->layout()) l->activate();
    const QPixmap shot = cropAlbum->grab();
    auto* veil = new QGraphicsOpacityEffect(cropAlbum);
    veil->setOpacity(0.0);
    cropAlbum->setGraphicsEffect(veil);
    QTimer::singleShot(0, btn, [this, btn, veil, shot, gen, host] {
      const auto lift = [btn, veil] {
        if (btn && btn->graphicsEffect() == veil) btn->setGraphicsEffect(nullptr);
      };
      if (!btn) return;
      if (gen != motion.gen || shot.isNull()) { lift(); return; }
      DisintegrateOverlay* fx = chipCloud(host, btn, shot, DisintegrateOverlay::Sweep::GATHER);
      if (!fx) { lift(); return; }
      const auto follow = [fx, btn, host] { if (btn && host) fx->move(btn->mapTo(host, QPoint())); };
      if (size.anim) connect(size.anim, &QVariantAnimation::valueChanged, fx, follow);
      QTimer::singleShot(int(CHIP_DUST_MS * FILTER_DUST_VEIL_STOP), btn, lift);
    });
  }

}  // namespace stencil::gui
