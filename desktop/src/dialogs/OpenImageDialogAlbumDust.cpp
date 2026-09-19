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

  // The Album/Portrait button's own materialize/fall — INLINE beside the always-shown
  // checkbox+caption, so unlike the two rows above it has no height of its own to slide:
  // just the same chip cloud. Browser twin: openImageModal.js's dust-only toggle.
  //
  // Both directions FOLLOW size_.anim: the button's own row (quickcropRow_) moves as the
  // window eases around the stage growing/shrinking above it, and a cloud raised once at
  // the button's STARTING position — the row's own row above it never moves this one —
  // was left behind mid-flight, stranded wherever the row happened to be at that instant
  // (measured: floating over the STAGE, well above the row it was meant to read as).
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
      // SYNCHRONOUS, like cropDims_'s own pin(0): syncCropStage() measures the window's
      // wanted height in THIS SAME call, right after this returns — a button still
      // occupying the row's width (deferred hiding, held at opacity 0 instead) read as
      // "still here" to that measure, so the window settled a whole wrapped caption LINE
      // taller than it should have and never came back down once the button actually left.
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
