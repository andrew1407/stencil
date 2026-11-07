#include "KeywordChips.hpp"
#include "chipClocks.hpp"
#include "../../../support/motion/DisintegrateOverlay.hpp"
#include "../../../support/control/FlowLayout.hpp"
#include "../../../support/motionPrefs.hpp"
#include "../../../support/uiTimings.hpp"
#include "../../../support/control/reveal/modeGhost.hpp"

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QFrame>
#include <QTimer>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

namespace stencil::gui {

  // The slot is held only while the chip fades into its grains; they go on falling over
  // the slide. Browser twin: the kwChipLeave keyframes' 14% / 15%.
  static constexpr double LEAVE_HOLD = 0.15;
  static constexpr double LEAVE_FADE = 0.14;
  // Sliding / Transition: the picture slides or shrinks away over the first half (browser kwChipSlideLeave 50%).
  static constexpr double MODE_LEAVE_HOLD = 0.5;
  // A 26px oval: the shared 7px dust cell reads as blocks on one (browser CHIP_MOTE_PX).
  static constexpr int CHIP_CELL_PX = 2;

  // The grid a chip-sized cloud wants, its budget DIVIDED by the chips flying at once, so
  // Clear all does not spawn a full cloud per chip (browser keywordChips.js chipGrid).
  static QSize chipGrid(const QSize& box, int sharing = 1) {
    const int cols = std::max(2, box.width() / CHIP_CELL_PX);
    const int rows = std::max(2, box.height() / CHIP_CELL_PX);
    const int budget = DisintegrateOverlay::SURFACE_MAX_CELLS / std::max(1, sharing);
    const double over = double(cols) * rows / std::max(1, budget);
    if (over <= 1.0) return QSize(cols, rows);
    const double k = std::sqrt(over);
    return QSize(std::max(2, int(cols / k)), std::max(2, int(rows / k)));
  }

  // Masked to the PILL: grab() hands back a rectangle, whose corners would scatter as a
  // block of background around the oval.
  static QPixmap pillShot(QFrame* chip) {
    QPixmap shot = chip->grab();
    if (shot.isNull()) return shot;
    QPixmap out(shot.size());
    out.setDevicePixelRatio(shot.devicePixelRatio());
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath pill;
    const QRectF r(0, 0, shot.width() / out.devicePixelRatio(), shot.height() / out.devicePixelRatio());
    pill.addRoundedRect(r, r.height() / 2.0, r.height() / 2.0);
    p.setClipPath(pill);
    p.drawPixmap(0, 0, shot);
    return out;
  }

  // One chip-sized cloud of its own pixels, in the pill's shape.
  static DisintegrateOverlay* chipDust(QFrame* chip, QWidget* host,
                                       DisintegrateOverlay::Sweep sweep, int ms, int sharing) {
    const QPixmap shot = pillShot(chip);
    if (shot.isNull()) return nullptr;
    const QSize g = chipGrid(chip->size(), sharing);
    return DisintegrateOverlay::overPixmaps(shot, QPixmap(), QRect(chip->mapTo(host, QPoint()),
                                                                   chip->size()),
                                            host, sweep, g.width(), g.height(), ms,
                                            1.0);   // spread: a list row's own throw
  }

  // The browser's kwChipLeave, in Qt: the chips scatter, their slots are HELD, and only then do
  // their widths collapse. Removing the widget outright made survivors jump over the grains.
  void KeywordChips::playLeave(const QList<QPointer<QFrame>>& going) {
    if (going.isEmpty()) return;
    const int leaveMs = keywordChipClocks().leaveMs;
    const bool dust = support::isDustAllowed();
    const bool mode = !dust && modeGhostMotion();
    const bool quiet = !dust && !mode;
    const double hold = mode ? MODE_LEAVE_HOLD : LEAVE_HOLD;
    for (QFrame* chip : going) {
      chip->setProperty("kwLeaving", true);   // no longer one of the list's chips
      if (quiet) { chip->setParent(nullptr); chip->deleteLater(); continue; }
      if (mode) {
        playModeGhost(chipArea, pillShot(chip), chip->geometry(), chipArea->rect(), /*in=*/false, Qt::Horizontal,
                      int(leaveMs * MODE_LEAVE_HOLD), {}, /*pill=*/true);
        veilBehindDust(chip)->setOpacity(0.0);   // the picture is the chip now; its slot stays to fold
        continue;
      }
      chipDust(chip, chipArea, DisintegrateOverlay::Sweep::FALL, leaveMs, going.size());
      // …and the chip goes with its grains rather than sitting opaque until the width
      // wipes, which read as dust over a solid chip followed by a right-to-left wipe.
      QGraphicsOpacityEffect* fade = veilBehindDust(chip);
      fade->setOpacity(1.0);
      auto* out = new QPropertyAnimation(fade, "opacity", chip);
      out->setDuration(support::motionMs(int(leaveMs * LEAVE_FADE)));
      out->setStartValue(1.0);
      out->setEndValue(0.0);
      out->setEasingCurve(QEasingCurve::OutCubic);
      out->start(QAbstractAnimation::DeleteWhenStopped);
    }
    if (quiet) { flow->activate(); return; }

    QList<int> widths;
    widths.reserve(going.size());
    for (QFrame* chip : going) widths << chip->width();
    // ONE animation for the whole batch: Clear all collapsed chip-by-chip otherwise, each
    // relayout shoving the rest sideways before their own turn came.
    auto* fold = new QVariantAnimation(this);
    fold->setDuration(support::motionMs(leaveMs));
    fold->setStartValue(0.0);
    fold->setEndValue(1.0);
    QPointer<KeywordChips> self(this);
    connect(fold, &QVariantAnimation::valueChanged, this, [self, going, widths, hold](const QVariant& v) {
      if (!self) return;
      const double t = v.toDouble();
      const double shrink = t <= hold ? 1.0 : (1.0 - t) / (1.0 - hold);
      for (int i = 0; i < going.size(); ++i) {
        if (!going[i]) continue;
        going[i]->setMinimumWidth(0);
        going[i]->setMaximumWidth(qMax(0, int(widths[i] * shrink)));
      }
      self->flow->invalidate();
      self->flow->activate();
    });
    connect(fold, &QAbstractAnimation::finished, this, [self, going] {
      for (QFrame* chip : going)
        if (chip) { chip->setParent(nullptr); chip->deleteLater(); }
      if (self) { self->flow->invalidate(); self->flow->activate(); }
    });
    fold->start(QAbstractAnimation::DeleteWhenStopped);
  }

  void KeywordChips::playMotion(const QHash<QString, QRect>& was, const QList<QFrame*>& arrived) {
    const bool dust = support::isDustAllowed();
    const bool mode = !dust && modeGhostMotion();
    if (!dust && !mode) return;   // "nothing may move" covers the glide too
    const support::FlipMotion& flip = support::flipMotion();
    for (auto it = chipFor.cbegin(); it != chipFor.cend(); ++it) {
      const QRect from = was.value(it.key());
      QFrame* chip = it.value();
      if (from.isNull() || from == chip->geometry()) continue;
      for (auto* old : chip->findChildren<QPropertyAnimation*>(QString(), Qt::FindDirectChildrenOnly))
        if (old->propertyName() == "geometry") old->stop();   // a newer edit's glide takes over
      chip->setProperty(FLOW_HELD, true);
      auto* fly = new QPropertyAnimation(chip, "geometry", chip);
      fly->setDuration(support::motionMs(flip.ms));
      fly->setEasingCurve(flip.easing);
      fly->setStartValue(from);
      fly->setEndValue(chip->geometry());
      connect(fly, &QAbstractAnimation::finished, this, [this, guard = QPointer<QFrame>(chip)] {
        if (guard) guard->setProperty(FLOW_HELD, false);
        flow->invalidate();
        flow->activate();   // into the slot it holds now, should the row have changed under the glide
      });
      fly->start(QAbstractAnimation::DeleteWhenStopped);
    }
    // A new chip gathers out of its own dust and only THEN fades up, held back while most of the
    // cloud lands. GATHER, not SURFACE_IN: the surface sweeps fly from a target point.
    const KeywordChipClocks& clocks = keywordChipClocks();
    // Sliding / Transition: the chip's picture slides in out of the well's edge or grows from 60%
    // while the rest glide aside (browser kwChipSlideIn / kwChipGrowIn); the chip shows as it lands.
    if (mode) {
      for (QFrame* chip : arrived) {
        const QPixmap shot = pillShot(chip);
        QGraphicsOpacityEffect* veil = veilBehindDust(chip);
        veil->setOpacity(0.0);
        QPointer<QFrame> guard(chip);
        // Shown at once as it lands, unveiled once the others' glide is over: dropping the effect re-lays the row,
        // which mid-glide snapped the others a pixel and back (user report).
        const int glideLeft = std::max(0, support::motionMs(flip.ms) - support::motionMs(clocks.enterMs));
        const auto land = [guard, veil = QPointer<QGraphicsOpacityEffect>(veil), glideLeft] {
          if (veil) veil->setOpacity(1.0);
          QTimer::singleShot(glideLeft, guard, [guard] { if (guard) guard->setGraphicsEffect(nullptr); });
        };
        if (!playModeGhost(chipArea, shot, chip->geometry(), chipArea->rect(), /*in=*/true, Qt::Horizontal,
                           clocks.enterMs, land, /*pill=*/true))
          chip->setGraphicsEffect(nullptr);
      }
      return;
    }
    for (QFrame* chip : arrived) {
      const bool dust = chipDust(chip, chipArea, DisintegrateOverlay::Sweep::GATHER, clocks.dustMs,
                                 arrived.size()) != nullptr;
      const int holdMs = dust ? clocks.enterDelayMs : 0;
      QGraphicsOpacityEffect* fade = veilBehindDust(chip);
      auto* up = new QPropertyAnimation(fade, "opacity", chip);
      up->setDuration(support::motionMs(holdMs + clocks.enterMs));
      up->setStartValue(0.0);
      up->setEndValue(1.0);
      up->setEasingCurve(chipEnterCurve(holdMs, clocks.enterMs));
      connect(up, &QAbstractAnimation::finished, chip, [chip] { chip->setGraphicsEffect(nullptr); });
      up->start(QAbstractAnimation::DeleteWhenStopped);
    }
  }

}  // namespace stencil::gui
