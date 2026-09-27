#include "KeywordChips.hpp"
#include "chipClocks.hpp"
#include "../../../support/motion/DisintegrateOverlay.hpp"
#include "../../../support/control/FlowLayout.hpp"
#include "../../../support/motionPrefs.hpp"
#include "../../../support/uiTimings.hpp"

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QFrame>
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
    const bool quiet = !support::isDustAllowed();
    for (QFrame* chip : going) {
      chip->setProperty("kwLeaving", true);   // no longer one of the list's chips
      if (quiet) { chip->setParent(nullptr); chip->deleteLater(); continue; }
      chipDust(chip, chipArea, DisintegrateOverlay::Sweep::FALL, leaveMs, going.size());
      // …and the chip goes with its grains rather than sitting opaque until the width
      // wipes, which read as dust over a solid chip followed by a right-to-left wipe.
      QGraphicsOpacityEffect* fade = veilBehindDust(chip);
      fade->setOpacity(1.0);
      auto* out = new QPropertyAnimation(fade, "opacity", chip);
      out->setDuration(int(leaveMs * LEAVE_FADE));
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
    fold->setDuration(leaveMs);
    fold->setStartValue(0.0);
    fold->setEndValue(1.0);
    QPointer<KeywordChips> self(this);
    connect(fold, &QVariantAnimation::valueChanged, this, [self, going, widths](const QVariant& v) {
      if (!self) return;
      const double t = v.toDouble();
      const double shrink = t <= LEAVE_HOLD ? 1.0 : (1.0 - t) / (1.0 - LEAVE_HOLD);
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
    if (!support::isDustAllowed()) return;   // "nothing may move" covers the glide too
    const support::FlipMotion& flip = support::flipMotion();
    for (auto it = chipFor.cbegin(); it != chipFor.cend(); ++it) {
      const QRect from = was.value(it.key());
      QFrame* chip = it.value();
      if (from.isNull() || from == chip->geometry()) continue;
      auto* fly = new QPropertyAnimation(chip, "geometry", chip);
      fly->setDuration(flip.ms);
      fly->setEasingCurve(flip.easing);
      fly->setStartValue(from);
      fly->setEndValue(chip->geometry());
      fly->start(QAbstractAnimation::DeleteWhenStopped);
    }
    // A new chip gathers out of its own dust and only THEN fades up, held back while most of the
    // cloud lands. GATHER, not SURFACE_IN: the surface sweeps fly from a target point.
    const KeywordChipClocks& clocks = keywordChipClocks();
    for (QFrame* chip : arrived) {
      const bool dust = chipDust(chip, chipArea, DisintegrateOverlay::Sweep::GATHER, clocks.dustMs,
                                 arrived.size()) != nullptr;
      const int holdMs = dust ? clocks.enterDelayMs : 0;
      QGraphicsOpacityEffect* fade = veilBehindDust(chip);
      auto* up = new QPropertyAnimation(fade, "opacity", chip);
      up->setDuration(holdMs + clocks.enterMs);
      up->setStartValue(0.0);
      up->setEndValue(1.0);
      up->setEasingCurve(chipEnterCurve(holdMs, clocks.enterMs));
      connect(up, &QAbstractAnimation::finished, chip, [chip] { chip->setGraphicsEffect(nullptr); });
      up->start(QAbstractAnimation::DeleteWhenStopped);
    }
  }

}  // namespace stencil::gui
