#include "../support/DisintegrateOverlay.hpp"
#include "../support/filterFade.hpp"
#include "../support/motionPrefs.hpp"
#include "OpenImageDialog.hpp"
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
  namespace {
    constexpr int CHIP_DUST_MS = 630;
    // The browser's CHIP_DUST_DRIFT. `spread` here IS that drift: both sides throw a mote
    // by (m-0.5)*66*k across and (26 + progress*30 + n*44)*k down (DisintegrateMotes.cpp /
    // motion/tiles.js tileMotion), so the same number is the same motion.
    constexpr double CHIP_DUST_DRIFT = 0.15;
    // KeywordChipsMotion.cpp CHIP_CELL_PX — the desktop's speck (browser CHIP_MOTE_PX 1.5).
    constexpr int CHIP_CELL_PX = 2;
  }

  // The preview's own ARRIVAL (browser ghostIn parity): the label stays veiled while an
  // overlay assembles it in its OWN colours — never a decoration over an already-visible
  // picture. With motion off the overlay no-ops, so veiling would just hide the picture.
  void OpenImageDialog::gatherPreviewDust(const QPixmap& shot) {
    QWidget* host = previewLabel_->parentWidget();
    if (!host || shot.isNull() || support::motionReduced()) return;
    auto* veil = new QGraphicsOpacityEffect(previewLabel_);
    veil->setOpacity(0.0);
    previewLabel_->setGraphicsEffect(veil);
    QPointer<QLabel> label(previewLabel_);
    const int gen = dustGen_;
    // After the height ease has LANDED — a cloud pinned mid-resize flies to where the label
    // was passing through — and after any departing picture has fallen: two clouds read as noise.
    const qint64 left = scatterEnds_ - QDateTime::currentMSecsSinceEpoch();
    QTimer::singleShot(std::max<qint64>(OI_RESIZE_MS, left), label,
                       [this, label, shot, veil, gen] {
      if (!label || gen != dustGen_) return;   // the tab moved on while this waited
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

  // The one thing a crop toggle adds to, or takes from, the column, so it arrives and leaves
  // on the KEYWORD-CHIP recipe (KeywordChipsMotion; the browser read-out flies the same).
  // Photographed before the veil (a veiled widget grabs to nothing) and once shown, since
  // only then does it have a size.
  void OpenImageDialog::cropDimsDust(bool arriving) {
    QWidget* host = cropDims_->parentWidget();
    // isVisible() cannot answer this any more: the line stays shown while it slides away.
    if (arriving == dimsShown_) return;   // a re-sync is not a state change
    dimsShown_ = arriving;
    if (!host || support::motionReduced() || quietCrop_) {
      if (dimsAnim_) dimsAnim_->stop();
      cropDims_->setMinimumHeight(0);
      cropDims_->setMaximumHeight(QWIDGETSIZE_MAX);
      cropDims_->setVisible(arriving);
      return;
    }
    // The mesh comes from the BOX, as the keyword chip's and the browser's do (dustGrid is
    // motion/tiles.js reshapeGrid): a fixed 64x22 budget over a 13px line made cells
    // 3.1 x 0.59, so the read-out flew as horizontal slices of itself instead of specks.
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
      const QPixmap shot = cropDims_->grab();   // before it goes: see below
      if (!shot.isNull()) chipCloud(cropDims_, shot, DisintegrateOverlay::Sweep::FALL);
      slideCropDims(false);
      return;
    }
    slideCropDims(true);
    if (QLayout* l = host->layout()) l->activate();
    const QPixmap shot = cropDims_->grab();
    auto* veil = new QGraphicsOpacityEffect(cropDims_);
    veil->setOpacity(0.0);
    cropDims_->setGraphicsEffect(veil);
    QPointer<QLabel> label(cropDims_);
    const int gen = dustGen_;
    // AFTER the window's height ease has landed: raised before it, the cloud is pinned where
    // the line USED to be and the growing column slides the line out from under it (measured
    // 19px off). The veil then lifts once most of the motes are home — not at the very end —
    // so the words are not missing for the whole flight.
    // ONE turn later, not synchronously: the resize syncCropStage just asked for lands when
    // this turn ends, so a cloud raised now is pinned where the line USED to be and the
    // column slides it out from under (measured 19px off). One turn is imperceptible — the
    // old wait was the whole height ease. The veil then lifts once most motes are home, so
    // the words are not missing for the entire play.
    QTimer::singleShot(0, label, [this, label, veil, shot, chipCloud, gen] {
      const auto lift = [label, veil] {
        if (label && label->graphicsEffect() == veil) label->setGraphicsEffect(nullptr);
      };
      if (!label) return;
      if (gen != dustGen_) { lift(); return; }
      DisintegrateOverlay* fx = shot.isNull()
          ? nullptr : chipCloud(label, shot, DisintegrateOverlay::Sweep::GATHER);
      if (!fx) {
        lift();
        return;
      }
      // The line it is assembling is still MOVING: the column slides it down as the window
      // eases, and the stage's first cropChanged re-words the read-out, which re-centres a
      // label of a different width. The layer follows it (browser twin: retargetDust).
      QWidget* under = label->parentWidget();
      const auto follow = [fx, label, under] {
        if (label && under) fx->move(label->mapTo(under, QPoint()));
      };
      if (dimsAnim_) connect(dimsAnim_, &QVariantAnimation::valueChanged, fx, follow);
      if (heightAnim_) connect(heightAnim_, &QVariantAnimation::valueChanged, fx, follow);
      QTimer::singleShot(int(CHIP_DUST_MS * FILTER_DUST_VEIL_STOP), label, lift);
    });
  }

  // The line SLIDES into its place and back out of it, on the window's own clock, so the
  // rows under it are never snapped up or down by its height (browser twin: syncCropDims'
  // height flight). The END state goes into the layout FIRST: the window's refit runs next
  // and must measure the shape this lands on, not the one it starts from.
  void OpenImageDialog::slideCropDims(bool show) {
    const int full = cropDims_->sizeHint().height();
    const int from = cropDims_->isVisible() && cropDims_->maximumHeight() < QWIDGETSIZE_MAX
                         ? cropDims_->maximumHeight()
                         : (show ? 0 : full);
    cropDims_->setVisible(true);
    const auto pin = [this](int h) {
      cropDims_->setMinimumHeight(h);   // a layout hands it its hint otherwise
      cropDims_->setMaximumHeight(h);
    };
    if (!dimsAnim_) {
      dimsAnim_ = new QVariantAnimation(this);
      dimsAnim_->setDuration(OI_RESIZE_MS);
      dimsAnim_->setEasingCurve(QEasingCurve::OutCubic);
      connect(dimsAnim_, &QVariantAnimation::valueChanged, this,
              [this, pin](const QVariant& v) { pin(v.toInt()); });
      connect(dimsAnim_, &QVariantAnimation::finished, this, [this] {
        const bool shown = dimsAnim_->endValue().toInt() > 0;
        cropDims_->setMinimumHeight(0);
        cropDims_->setMaximumHeight(shown ? QWIDGETSIZE_MAX : 0);
        cropDims_->setVisible(shown);
      });
    }
    dimsAnim_->stop();
    dimsAnim_->setStartValue(from);
    dimsAnim_->setEndValue(show ? full : 0);
    // The END state into the layout LAST: setStartValue emits its value straight away, so
    // pinning before it left the line's old height in the layout for the refit to measure.
    pin(show ? full : 0);
    // Started ONE TURN LATER: start() emits its first value at once, which would put the
    // line's old height back into the layout before the caller's refit measures it — and
    // the window then settled a line short of its own content, both ways.
    QTimer::singleShot(0, dimsAnim_, [this] {
      if (dimsAnim_ && dimsAnim_->state() != QAbstractAnimation::Running) dimsAnim_->start();
    });
  }

  // A DIFFERENT source replacing the picture on screen: the old blows away first so the
  // two never cross-fade. The new one's arrival is its own play, once its decode lands.
  void OpenImageDialog::scatterPreviewDust() {
    QWidget* host = previewLabel_->parentWidget();
    if (!previewLabel_->isVisible() || previewLabel_->pixmap().isNull()) return;
    const QPixmap shot = previewLabel_->grab(previewLabel_->rect());
    const QRect box = previewLabel_->rect();
    // Raised while the label is STILL SHOWN (overRect refuses an invisible source); the room
    // it took goes straight after, the cloud carrying the picture on.
    if (host && !shot.isNull() && !support::motionReduced()) {
      scatterEnds_ = QDateTime::currentMSecsSinceEpoch() + CANVAS_DUST_MS;
      arrivalDue_ = true;   // whatever lands next flies in, seen before or not
      DisintegrateOverlay::overRect(previewLabel_, box, host, DisintegrateOverlay::Sweep::FALL,
                                    false, DisintegrateOverlay::DUST_MAX_CELLS, CANVAS_DUST_MS,
                                    QColor(), shot);
    }
    clearPreviewImage();
  }

  // A cloud is a child of the column, not of the picture it came from, so switching tab or
  // dismissing the popover left the motes playing over whatever arrived next. Every veil a
  // cloud was standing in for lifts with it, or the widget it covers stays invisible.
  void OpenImageDialog::cancelPreviewDust() {
    if (!previewLabel_) return;
    ++dustGen_;   // a raise still queued behind a timer is now stale
    for (QWidget* w : findChildren<QWidget*>(
             QString::fromLatin1(DisintegrateOverlay::OBJECT_NAME)))
      delete w;
    previewLabel_->setGraphicsEffect(nullptr);
    if (cropDims_) cropDims_->setGraphicsEffect(nullptr);
    if (cropSizeRow_) cropSizeRow_->setGraphicsEffect(nullptr);
    if (cropAlbum_) cropAlbum_->setGraphicsEffect(nullptr);
    if (cropSizeCustomGroup_) cropSizeCustomGroup_->setGraphicsEffect(nullptr);
    scatterEnds_ = 0;
  }

  // reject() hides the dialog while the popover's own collapse still runs, so an
  // unfinished cloud played on over the closing panel.
  void OpenImageDialog::hideEvent(QHideEvent* event) {
    cancelPreviewDust();
    QDialog::hideEvent(event);
  }

}  // namespace stencil::gui
