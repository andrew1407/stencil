// The shared chip-dust recipe: one cloud per row, and the height slide that goes with it.
#include "chipDust.hpp"
#include "easeWindowHeight.hpp"
#include "../../../support/motionPrefs.hpp"
#include "../../../support/genie/GenieGhost.hpp"
#include <algorithm>
#include <cmath>
#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

namespace stencil::gui {

  DisintegrateOverlay* chipCloud(QWidget* host, QWidget* w, const QPixmap& shot,
                                 DisintegrateOverlay::Sweep sweep) {
    int cols = 0, rows = 0;
    DisintegrateOverlay::dustGrid(w->size(), CHIP_CELL_PX,
                                  DisintegrateOverlay::SURFACE_MAX_CELLS, &cols, &rows);
    return DisintegrateOverlay::overPixmaps(shot, QPixmap(),
                                            QRect(w->mapTo(host, QPoint()), w->size()), host,
                                            sweep, cols, rows, keywordChipClocks().dustMs,
                                            keywordChipClocks().dustDrift);
  }

  QWidget* modeFlight(QWidget* host, QWidget* w, const QPixmap& shot, bool show, int ms) {
    const bool slide = support::isSlideMotionOk();
    if (!host || !w || shot.isNull() || !(slide || support::isTransitionMotionOk())) return nullptr;
    auto* clip = new QWidget(host);   // the box the picture is cut to
    clip->setObjectName(QString::fromLatin1(DisintegrateOverlay::OBJECT_NAME));   // swept with the clouds
    clip->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* ghost = new QLabel(clip);
    ghost->setScaledContents(true);
    ghost->setPixmap(shot);
    auto* fade = new QGraphicsOpacityEffect(ghost);
    ghost->setGraphicsEffect(fade);
    QGraphicsOpacityEffect* veil = show ? veilBehindDust(w) : nullptr;
    QPointer<QWidget> src(w);
    const QSize full = shot.deviceIndependentSize().toSize();
    clip->setGeometry(QRect(w->mapTo(host, QPoint()), w->size()));
    // t: 0 = away, 1 = home; the clip follows the widget while it is still laid out.
    const auto place = [clip, ghost, fade, src, host, full, slide](double t) {
      if (src && src->isVisible()) clip->setGeometry(QRect(src->mapTo(host, QPoint()), src->size()));
      if (slide) {
        ghost->setGeometry(static_cast<int>(std::lround(-1.04 * full.width() * (1.0 - t))), 0, full.width(), full.height());
        return;
      }
      const double s = 0.78 + 0.22 * t;
      const QSize sz(std::max(1, static_cast<int>(full.width() * s)), std::max(1, static_cast<int>(full.height() * s)));
      ghost->setGeometry(QRect(QPoint((full.width() - sz.width()) / 2, (full.height() - sz.height()) / 2), sz));
      fade->setOpacity(t);
    };
    place(show ? 0.0 : 1.0);
    clip->show();
    clip->raise();
    auto* anim = new QVariantAnimation(clip);
    anim->setStartValue(show ? 0.0 : 1.0);
    anim->setEndValue(show ? 1.0 : 0.0);
    // browser row.js: an arrival eases out, a departure accelerates away
    anim->setEasingCurve(show ? QEasingCurve::OutExpo : QEasingCurve::InCubic);
    QObject::connect(anim, &QVariantAnimation::valueChanged, clip, [place](const QVariant& v) { place(v.toDouble()); });
    QObject::connect(clip, &QObject::destroyed, w, [src, veil] {
      if (src && veil && src->graphicsEffect() == veil) src->setGraphicsEffect(nullptr);
    });
    QObject::connect(anim, &QVariantAnimation::finished, clip, &QObject::deleteLater);
    support::startTuned(anim, ms, QAbstractAnimation::DeleteWhenStopped);
    return clip;
  }

  int mediaFlight(QWidget* host, QWidget* w, const QPixmap& shot, bool show, QWidget* origin) {
    const bool slide = support::isSlideMotionOk();
    if (!host || !w || !origin || shot.isNull() || !(slide || support::isTransitionMotionOk())) return 0;
    const QRect at(w->mapTo(host, QPoint()), w->size()), from(origin->mapTo(host, QPoint()), origin->size());
    QGraphicsOpacityEffect* veil = show ? veilBehindDust(w) : nullptr;
    QPointer<QWidget> src(w);
    const auto lift = [src, veil] { if (src && veil && src->graphicsEffect() == veil) src->setGraphicsEffect(nullptr); };
    if (slide) {
      const QPoint g = host->mapToGlobal(QPoint());
      if (support::genie::fly(host, shot, at.translated(g), from.translated(g), show, false, lift)) {
        host->children().constLast()->setObjectName(QString::fromLatin1(DisintegrateOverlay::OBJECT_NAME));
        return support::motionMs(show ? support::genie::OPEN_MS : support::genie::CLOSE_MS);
      }
      lift();
      return 0;
    }
    auto* ghost = new QLabel(host);   // Transition: the picture grows out of the control's box, fading up
    ghost->setObjectName(QString::fromLatin1(DisintegrateOverlay::OBJECT_NAME));
    ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
    ghost->setScaledContents(true);
    ghost->setPixmap(shot);
    auto* fade = new QGraphicsOpacityEffect(ghost);
    ghost->setGraphicsEffect(fade);
    const auto place = [ghost, fade, at, from](double t) {   // t: 0 = in the control, 1 = home
      const auto mix = [t](int a, int b) { return static_cast<int>(std::lround(a + (b - a) * t)); };
      ghost->setGeometry(mix(from.x(), at.x()), mix(from.y(), at.y()), std::max(1, mix(from.width(), at.width())),
                         std::max(1, mix(from.height(), at.height())));
      fade->setOpacity(std::min(1.0, t * 2.0));
    };
    place(show ? 0.0 : 1.0);
    ghost->show();
    ghost->raise();
    auto* anim = new QVariantAnimation(ghost);
    anim->setStartValue(show ? 0.0 : 1.0);
    anim->setEndValue(show ? 1.0 : 0.0);
    anim->setEasingCurve(show ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    QObject::connect(anim, &QVariantAnimation::valueChanged, ghost, [place](const QVariant& v) { place(v.toDouble()); });
    QObject::connect(ghost, &QObject::destroyed, w, lift);
    QObject::connect(anim, &QVariantAnimation::finished, ghost, &QObject::deleteLater);
    support::startTuned(anim, support::WINDOW_RESIZE_MS, QAbstractAnimation::DeleteWhenStopped);
    return support::motionMs(support::WINDOW_RESIZE_MS);
  }

  void slideRowHeight(QObject* owner, QWidget* row, QVariantAnimation*& anim, bool show) {
    const int full = row->sizeHint().height();
    const int from = row->isVisible() && row->maximumHeight() < QWIDGETSIZE_MAX
                         ? row->maximumHeight()
                         : (show ? 0 : full);
    row->setVisible(true);
    const auto pin = [row](int h) {
      row->setMinimumHeight(h);   // a layout hands it its hint otherwise
      row->setMaximumHeight(h);
    };
    if (!anim) {
      anim = new QVariantAnimation(owner);
      anim->setDuration(support::WINDOW_RESIZE_MS);
      anim->setEasingCurve(QEasingCurve::OutCubic);
      QVariantAnimation* a = anim;
      QObject::connect(a, &QVariantAnimation::valueChanged, owner,
                       [pin](const QVariant& v) { pin(v.toInt()); });
      QObject::connect(a, &QVariantAnimation::finished, owner, [a, row] {
        const bool shown = a->endValue().toInt() > 0;
        row->setMinimumHeight(0);
        row->setMaximumHeight(shown ? QWIDGETSIZE_MAX : 0);
        row->setVisible(shown);
      });
    }
    anim->stop();
    anim->setDuration(support::motionMs(support::WINDOW_RESIZE_MS));
    anim->setStartValue(from);
    anim->setEndValue(show ? full : 0);
    // The END state into the layout LAST: setStartValue emits its value straight away, so
    // pinning before it left the line's old height in the layout for the refit to measure.
    pin(show ? full : 0);
    // Started ONE TURN LATER: start() emits its first value at once, which would put the line's old
    // height back into the layout before the caller's refit measures it.
    QVariantAnimation* a = anim;
    QTimer::singleShot(0, a, [a] {
      if (a->state() != QAbstractAnimation::Running) a->start();
    });
  }

}  // namespace stencil::gui
