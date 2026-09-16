#include "KeywordChips.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "../support/FlowLayout.hpp"
#include "../support/motionPrefs.hpp"

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

namespace stencil::gui {

  // The chip clocks, 1.5x the base beat so the grains read. Browser twins: the kwChipLeave
  // keyframes and motion/tune.js FLIP_MS x 1.5.
  static constexpr int GLIDE_MS = 390;
  static constexpr int ENTER_MS = 630;
  static constexpr int LEAVE_MS = 630;
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

  // The browser's kwChipLeave, in Qt: the chips scatter, their slots are HELD while that
  // reads, and only then do their widths collapse — which is what slides the chips behind
  // them. Removing the widget outright made the survivors jump over the falling grains.
  void KeywordChips::playLeave(const QList<QPointer<QFrame>>& going) {
    if (going.isEmpty()) return;
    const bool quiet = !support::isDustAllowed();
    for (QFrame* chip : going) {
      chip->setProperty("kwLeaving", true);   // no longer one of the list's chips
      if (quiet) { chip->setParent(nullptr); chip->deleteLater(); continue; }
      chipDust(chip, chipArea_, DisintegrateOverlay::Sweep::FALL, LEAVE_MS, going.size());
      // …and the chip goes with its grains rather than sitting opaque until the width
      // wipes, which read as dust over a solid chip followed by a right-to-left wipe.
      auto* fade = new QGraphicsOpacityEffect(chip);
      chip->setGraphicsEffect(fade);
      auto* out = new QPropertyAnimation(fade, "opacity", chip);
      out->setDuration(int(LEAVE_MS * LEAVE_FADE));
      out->setStartValue(1.0);
      out->setEndValue(0.0);
      out->setEasingCurve(QEasingCurve::OutCubic);
      out->start(QAbstractAnimation::DeleteWhenStopped);
    }
    if (quiet) { flow_->activate(); return; }

    QList<int> widths;
    widths.reserve(going.size());
    for (QFrame* chip : going) widths << chip->width();
    // ONE animation for the whole batch: Clear all collapsed chip-by-chip otherwise, each
    // relayout shoving the rest sideways before their own turn came.
    auto* fold = new QVariantAnimation(this);
    fold->setDuration(LEAVE_MS);
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
      self->flow_->invalidate();
      self->flow_->activate();
    });
    connect(fold, &QAbstractAnimation::finished, this, [self, going] {
      for (QFrame* chip : going)
        if (chip) { chip->setParent(nullptr); chip->deleteLater(); }
      if (self) { self->flow_->invalidate(); self->flow_->activate(); }
    });
    fold->start(QAbstractAnimation::DeleteWhenStopped);
  }

  void KeywordChips::playMotion(const QHash<QString, QRect>& was, const QList<QFrame*>& arrived) {
    if (!support::isDustAllowed()) return;   // "nothing may move" covers the glide too
    for (auto it = chipFor_.cbegin(); it != chipFor_.cend(); ++it) {
      const QRect from = was.value(it.key());
      QFrame* chip = it.value();
      if (from.isNull() || from == chip->geometry()) continue;
      auto* fly = new QPropertyAnimation(chip, "geometry", chip);
      fly->setDuration(GLIDE_MS);
      fly->setEasingCurve(QEasingCurve::OutCubic);
      fly->setStartValue(from);
      fly->setEndValue(chip->geometry());
      fly->start(QAbstractAnimation::DeleteWhenStopped);
    }
    // A new chip gathers out of its own dust and only THEN fades up: holdFadeKeys is the
    // app's "stay invisible behind the cloud" curve, the one a revealed dialog uses.
    // GATHER, not SURFACE_IN: the surface sweeps fly from a target point, so a chip
    // gathered in from the window's corner. GATHER is FALL rewound.
    for (QFrame* chip : arrived) {
      chipDust(chip, chipArea_, DisintegrateOverlay::Sweep::GATHER, ENTER_MS, arrived.size());
      auto* fade = new QGraphicsOpacityEffect(chip);
      fade->setOpacity(0.0);
      chip->setGraphicsEffect(fade);
      auto* up = new QPropertyAnimation(fade, "opacity", chip);
      holdFadeKeys(up, ENTER_MS);
      connect(up, &QAbstractAnimation::finished, chip, [chip] { chip->setGraphicsEffect(nullptr); });
      up->start(QAbstractAnimation::DeleteWhenStopped);
    }
  }

}  // namespace stencil::gui
