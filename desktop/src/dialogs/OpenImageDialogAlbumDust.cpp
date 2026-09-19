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

  // The Album/Portrait button materializes and falls INLINE beside the always-shown checkbox, so
  // unlike the rows above it has no height to slide — just the chip cloud. Both directions FOLLOW
  // size_.anim, or a cloud raised at the button's start position is stranded over the stage.
  void OpenImageDialog::cropAlbumDust(bool arriving) {
    if (arriving == motion_.albumShown) return;
    motion_.albumShown = arriving;
    QWidget* host = cropAlbum_->parentWidget();
    if (!host || support::motionReduced() || motion_.quietCrop) {
      cropAlbum_->setVisible(arriving);
      return;
    }
    QPointer<QPushButton> btn(cropAlbum_);
    const int gen = motion_.gen;
    if (!arriving) {
      // SYNCHRONOUS, like cropDims_'s own pin(0): syncCropStage() measures the window's wanted height in
      // THIS SAME call. A button still occupying the row's width (deferred hiding at opacity 0) read as
      // "still here", so the window settled a wrapped caption LINE taller and never came back down.
      const QPixmap shot = cropAlbum_->grab();   // before it goes
      cropAlbum_->setVisible(false);
      if (!shot.isNull()) {
        DisintegrateOverlay* fx = chipCloud(host, cropAlbum_, shot, DisintegrateOverlay::Sweep::FALL);
        if (fx && size_.anim) {
          const auto follow = [fx, btn, host] { if (btn && host) fx->move(btn->mapTo(host, QPoint())); };
          connect(size_.anim, &QVariantAnimation::valueChanged, fx, follow);
        }
      }
      return;
    }
    cropAlbum_->setVisible(true);
    if (QLayout* l = host->layout()) l->activate();
    const QPixmap shot = cropAlbum_->grab();
    auto* veil = new QGraphicsOpacityEffect(cropAlbum_);
    veil->setOpacity(0.0);
    cropAlbum_->setGraphicsEffect(veil);
    QTimer::singleShot(0, btn, [this, btn, veil, shot, gen, host] {
      const auto lift = [btn, veil] {
        if (btn && btn->graphicsEffect() == veil) btn->setGraphicsEffect(nullptr);
      };
      if (!btn) return;
      if (gen != motion_.gen || shot.isNull()) { lift(); return; }
      DisintegrateOverlay* fx = chipCloud(host, btn, shot, DisintegrateOverlay::Sweep::GATHER);
      if (!fx) { lift(); return; }
      const auto follow = [fx, btn, host] { if (btn && host) fx->move(btn->mapTo(host, QPoint())); };
      if (size_.anim) connect(size_.anim, &QVariantAnimation::valueChanged, fx, follow);
      QTimer::singleShot(int(CHIP_DUST_MS * FILTER_DUST_VEIL_STOP), btn, lift);
    });
  }

}  // namespace stencil::gui
