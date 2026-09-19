#include "../support/DisintegrateOverlay.hpp"
#include "../support/filterFade.hpp"
#include "../support/motionPrefs.hpp"
#include "OpenImageDialog.hpp"
#include "openImageDialogParts.hpp"
#include <QGraphicsOpacityEffect>
#include <QLayout>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

// The crop's page-size row and the Album/Portrait button's own particle show/hide — split
// out of OpenImageDialogDust.cpp (size budget) but on the SAME keyword-chip recipe as
// cropDimsDust/slideCropDims there.
namespace stencil::gui {

  namespace {
    constexpr int CHIP_DUST_MS = 630;
    constexpr double CHIP_DUST_DRIFT = 0.15;
    constexpr int CHIP_CELL_PX = 2;
  }

  // The page-size row's own arrival/departure — same recipe as cropDimsDust, a second
  // widget on its own clock (dust + slide can be mid-flight on both rows at once).
  void OpenImageDialog::cropSizeRowDust(bool arriving) {
    QWidget* host = cropSizeRow_->parentWidget();
    if (arriving == cropSizeRowShown_) return;
    cropSizeRowShown_ = arriving;
    if (!host || support::motionReduced() || quietCrop_) {
      if (cropSizeRowAnim_) cropSizeRowAnim_->stop();
      cropSizeRow_->setMinimumHeight(0);
      cropSizeRow_->setMaximumHeight(QWIDGETSIZE_MAX);
      cropSizeRow_->setVisible(arriving);
      return;
    }
    const auto chipCloud = [host](QWidget* w, const QPixmap& shot,
                                  DisintegrateOverlay::Sweep sweep) {
      int cols = 0, rows = 0;
      DisintegrateOverlay::dustGrid(w->size(), CHIP_CELL_PX,
                                    DisintegrateOverlay::SURFACE_MAX_CELLS, &cols, &rows);
      return DisintegrateOverlay::overPixmaps(shot, QPixmap(),
                                              QRect(w->mapTo(host, QPoint()), w->size()), host,
                                              sweep, cols, rows, CHIP_DUST_MS, CHIP_DUST_DRIFT);
    };
    if (!arriving) {
      const QPixmap shot = cropSizeRow_->grab();
      if (!shot.isNull()) chipCloud(cropSizeRow_, shot, DisintegrateOverlay::Sweep::FALL);
      slideCropSizeRow(false);
      return;
    }
    slideCropSizeRow(true);
    if (QLayout* l = host->layout()) l->activate();
    const QPixmap shot = cropSizeRow_->grab();
    auto* veil = new QGraphicsOpacityEffect(cropSizeRow_);
    veil->setOpacity(0.0);
    cropSizeRow_->setGraphicsEffect(veil);
    QPointer<QWidget> row(cropSizeRow_);
    const int gen = dustGen_;
    QTimer::singleShot(0, row, [this, row, veil, shot, chipCloud, gen] {
      const auto lift = [row, veil] {
        if (row && row->graphicsEffect() == veil) row->setGraphicsEffect(nullptr);
      };
      if (!row) return;
      if (gen != dustGen_) { lift(); return; }
      DisintegrateOverlay* fx = shot.isNull()
          ? nullptr : chipCloud(row, shot, DisintegrateOverlay::Sweep::GATHER);
      if (!fx) { lift(); return; }
      QWidget* under = row->parentWidget();
      const auto follow = [fx, row, under] {
        if (row && under) fx->move(row->mapTo(under, QPoint()));
      };
      if (cropSizeRowAnim_) connect(cropSizeRowAnim_, &QVariantAnimation::valueChanged, fx, follow);
      if (heightAnim_) connect(heightAnim_, &QVariantAnimation::valueChanged, fx, follow);
      QTimer::singleShot(int(CHIP_DUST_MS * FILTER_DUST_VEIL_STOP), row, lift);
    });
  }

  // Same clock and pin trick as slideCropDims — see there for the two timing notes
  // (start-from-current-height, and the one-turn-later start) this mirrors exactly.
  void OpenImageDialog::slideCropSizeRow(bool show) {
    const int full = cropSizeRow_->sizeHint().height();
    const int from = cropSizeRow_->isVisible() && cropSizeRow_->maximumHeight() < QWIDGETSIZE_MAX
                         ? cropSizeRow_->maximumHeight()
                         : (show ? 0 : full);
    cropSizeRow_->setVisible(true);
    const auto pin = [this](int h) {
      cropSizeRow_->setMinimumHeight(h);
      cropSizeRow_->setMaximumHeight(h);
    };
    if (!cropSizeRowAnim_) {
      cropSizeRowAnim_ = new QVariantAnimation(this);
      cropSizeRowAnim_->setDuration(OI_RESIZE_MS);
      cropSizeRowAnim_->setEasingCurve(QEasingCurve::OutCubic);
      connect(cropSizeRowAnim_, &QVariantAnimation::valueChanged, this,
              [pin](const QVariant& v) { pin(v.toInt()); });
      connect(cropSizeRowAnim_, &QVariantAnimation::finished, this, [this] {
        const bool shown = cropSizeRowAnim_->endValue().toInt() > 0;
        cropSizeRow_->setMinimumHeight(0);
        cropSizeRow_->setMaximumHeight(shown ? QWIDGETSIZE_MAX : 0);
        cropSizeRow_->setVisible(shown);
      });
    }
    cropSizeRowAnim_->stop();
    cropSizeRowAnim_->setStartValue(from);
    cropSizeRowAnim_->setEndValue(show ? full : 0);
    pin(show ? full : 0);
    QTimer::singleShot(0, cropSizeRowAnim_, [this] {
      if (cropSizeRowAnim_ && cropSizeRowAnim_->state() != QAbstractAnimation::Running)
        cropSizeRowAnim_->start();
    });
  }

  // The Album/Portrait button's own materialize/fall — INLINE beside the always-shown
  // checkbox+caption, so unlike the two rows above it has no height of its own to slide:
  // just the same chip cloud. Browser twin: openImageModal.js's dust-only toggle.
  //
  // Both directions FOLLOW heightAnim_: the button's own row (quickcropRow_) moves as the
  // window eases around the stage growing/shrinking above it, and a cloud raised once at
  // the button's STARTING position — the row's own row above it never moves this one —
  // was left behind mid-flight, stranded wherever the row happened to be at that instant
  // (measured: floating over the STAGE, well above the row it was meant to read as).
  void OpenImageDialog::cropAlbumDust(bool arriving) {
    if (arriving == cropAlbumShown_) return;
    cropAlbumShown_ = arriving;
    QWidget* host = cropAlbum_->parentWidget();
    if (!host || support::motionReduced() || quietCrop_) {
      cropAlbum_->setVisible(arriving);
      return;
    }
    const auto chipCloud = [host](QWidget* w, const QPixmap& shot,
                                  DisintegrateOverlay::Sweep sweep) {
      int cols = 0, rows = 0;
      DisintegrateOverlay::dustGrid(w->size(), CHIP_CELL_PX,
                                    DisintegrateOverlay::SURFACE_MAX_CELLS, &cols, &rows);
      return DisintegrateOverlay::overPixmaps(shot, QPixmap(),
                                              QRect(w->mapTo(host, QPoint()), w->size()), host,
                                              sweep, cols, rows, CHIP_DUST_MS, CHIP_DUST_DRIFT);
    };
    QPointer<QPushButton> btn(cropAlbum_);
    const int gen = dustGen_;
    if (!arriving) {
      // SYNCHRONOUS, like cropDims_'s own pin(0): syncCropStage() measures the window's
      // wanted height in THIS SAME call, right after this returns — a button still
      // occupying the row's width (deferred hiding, held at opacity 0 instead) read as
      // "still here" to that measure, so the window settled a whole wrapped caption LINE
      // taller than it should have and never came back down once the button actually left.
      const QPixmap shot = cropAlbum_->grab();   // before it goes
      cropAlbum_->setVisible(false);
      if (!shot.isNull()) {
        DisintegrateOverlay* fx = chipCloud(cropAlbum_, shot, DisintegrateOverlay::Sweep::FALL);
        if (fx && heightAnim_) {
          const auto follow = [fx, btn, host] { if (btn && host) fx->move(btn->mapTo(host, QPoint())); };
          connect(heightAnim_, &QVariantAnimation::valueChanged, fx, follow);
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
    QTimer::singleShot(0, btn, [this, btn, veil, shot, chipCloud, gen, host] {
      const auto lift = [btn, veil] {
        if (btn && btn->graphicsEffect() == veil) btn->setGraphicsEffect(nullptr);
      };
      if (!btn) return;
      if (gen != dustGen_ || shot.isNull()) { lift(); return; }
      DisintegrateOverlay* fx = chipCloud(btn, shot, DisintegrateOverlay::Sweep::GATHER);
      if (!fx) { lift(); return; }
      const auto follow = [fx, btn, host] { if (btn && host) fx->move(btn->mapTo(host, QPoint())); };
      if (heightAnim_) connect(heightAnim_, &QVariantAnimation::valueChanged, fx, follow);
      QTimer::singleShot(int(CHIP_DUST_MS * FILTER_DUST_VEIL_STOP), btn, lift);
    });
  }

  // The Custom W×H group's own arrival/departure — same recipe as cropSizeRowDust, on its
  // own clock, so picking Custom (or leaving it) plays independently of the row around it.
  // Browser twin: openImageModal.js's syncCropSizeCustom.
  void OpenImageDialog::cropSizeCustomDust(bool arriving) {
    QWidget* host = cropSizeCustomGroup_->parentWidget();
    if (arriving == cropSizeCustomShown_) return;
    cropSizeCustomShown_ = arriving;
    if (!host || support::motionReduced() || quietCrop_) {
      if (cropSizeCustomAnim_) cropSizeCustomAnim_->stop();
      cropSizeCustomGroup_->setMinimumHeight(0);
      cropSizeCustomGroup_->setMaximumHeight(QWIDGETSIZE_MAX);
      cropSizeCustomGroup_->setVisible(arriving);
      return;
    }
    const auto chipCloud = [host](QWidget* w, const QPixmap& shot,
                                  DisintegrateOverlay::Sweep sweep) {
      int cols = 0, rows = 0;
      DisintegrateOverlay::dustGrid(w->size(), CHIP_CELL_PX,
                                    DisintegrateOverlay::SURFACE_MAX_CELLS, &cols, &rows);
      return DisintegrateOverlay::overPixmaps(shot, QPixmap(),
                                              QRect(w->mapTo(host, QPoint()), w->size()), host,
                                              sweep, cols, rows, CHIP_DUST_MS, CHIP_DUST_DRIFT);
    };
    if (!arriving) {
      const QPixmap shot = cropSizeCustomGroup_->grab();
      if (!shot.isNull()) chipCloud(cropSizeCustomGroup_, shot, DisintegrateOverlay::Sweep::FALL);
      slideCropSizeCustom(false);
      return;
    }
    slideCropSizeCustom(true);
    if (QLayout* l = host->layout()) l->activate();
    const QPixmap shot = cropSizeCustomGroup_->grab();
    auto* veil = new QGraphicsOpacityEffect(cropSizeCustomGroup_);
    veil->setOpacity(0.0);
    cropSizeCustomGroup_->setGraphicsEffect(veil);
    QPointer<QWidget> row(cropSizeCustomGroup_);
    const int gen = dustGen_;
    QTimer::singleShot(0, row, [this, row, veil, shot, chipCloud, gen] {
      const auto lift = [row, veil] {
        if (row && row->graphicsEffect() == veil) row->setGraphicsEffect(nullptr);
      };
      if (!row) return;
      if (gen != dustGen_) { lift(); return; }
      DisintegrateOverlay* fx = shot.isNull()
          ? nullptr : chipCloud(row, shot, DisintegrateOverlay::Sweep::GATHER);
      if (!fx) { lift(); return; }
      QWidget* under = row->parentWidget();
      const auto follow = [fx, row, under] {
        if (row && under) fx->move(row->mapTo(under, QPoint()));
      };
      if (cropSizeCustomAnim_) connect(cropSizeCustomAnim_, &QVariantAnimation::valueChanged, fx, follow);
      if (heightAnim_) connect(heightAnim_, &QVariantAnimation::valueChanged, fx, follow);
      QTimer::singleShot(int(CHIP_DUST_MS * FILTER_DUST_VEIL_STOP), row, lift);
    });
  }

  // Same clock and pin trick as slideCropSizeRow.
  void OpenImageDialog::slideCropSizeCustom(bool show) {
    const int full = cropSizeCustomGroup_->sizeHint().height();
    const int from = cropSizeCustomGroup_->isVisible()
                          && cropSizeCustomGroup_->maximumHeight() < QWIDGETSIZE_MAX
                         ? cropSizeCustomGroup_->maximumHeight()
                         : (show ? 0 : full);
    cropSizeCustomGroup_->setVisible(true);
    const auto pin = [this](int h) {
      cropSizeCustomGroup_->setMinimumHeight(h);
      cropSizeCustomGroup_->setMaximumHeight(h);
    };
    if (!cropSizeCustomAnim_) {
      cropSizeCustomAnim_ = new QVariantAnimation(this);
      cropSizeCustomAnim_->setDuration(OI_RESIZE_MS);
      cropSizeCustomAnim_->setEasingCurve(QEasingCurve::OutCubic);
      connect(cropSizeCustomAnim_, &QVariantAnimation::valueChanged, this,
              [pin](const QVariant& v) { pin(v.toInt()); });
      connect(cropSizeCustomAnim_, &QVariantAnimation::finished, this, [this] {
        const bool shown = cropSizeCustomAnim_->endValue().toInt() > 0;
        cropSizeCustomGroup_->setMinimumHeight(0);
        cropSizeCustomGroup_->setMaximumHeight(shown ? QWIDGETSIZE_MAX : 0);
        cropSizeCustomGroup_->setVisible(shown);
      });
    }
    cropSizeCustomAnim_->stop();
    cropSizeCustomAnim_->setStartValue(from);
    cropSizeCustomAnim_->setEndValue(show ? full : 0);
    pin(show ? full : 0);
    QTimer::singleShot(0, cropSizeCustomAnim_, [this] {
      if (cropSizeCustomAnim_ && cropSizeCustomAnim_->state() != QAbstractAnimation::Running)
        cropSizeCustomAnim_->start();
    });
  }

}  // namespace stencil::gui
