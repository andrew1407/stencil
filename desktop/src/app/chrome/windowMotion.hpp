#pragma once

// The window's own motion: a dock's extent slide, the chat's opening extent and the window fade-in.

#include "../../support/dust/dustKit.hpp"   // support::frameIntervalMs
#include "../../support/motionPrefs.hpp"   // support::motionMs

#include <QEasingCurve>
#include <QElapsedTimer>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>
#include <algorithm>
#include <functional>

namespace stencil::gui {

  // The chat dock's extent as it opens: the one it last had, else the browser's default (px).
  inline int chatOpenExtent(int remembered, bool horiz) { return remembered > 80 ? remembered : (horiz ? 345 : 320); }

  // QMainWindow overrides a child's min/max during its own layout passes, so `apply` pins every frame and `done` releases.
  // One frame per screen refresh (support::frameIntervalMs), as the browser's slide gets, not Qt's fixed 16ms:
  // held paused and stepped by its own timer; stop() still cancels it without `done`.
  inline QVariantAnimation* startExtentSlide(QObject* owner, int from, int to, int ms,
                                             std::function<void(int)> apply,
                                             std::function<void()> done,
                                             const QEasingCurve& easing = QEasingCurve::OutCubic) {
    auto* anim = new QVariantAnimation(owner);
    anim->setDuration(support::motionMs(ms));
    anim->setEasingCurve(easing);
    anim->setStartValue(from);
    anim->setEndValue(to);
    QObject::connect(anim, &QVariantAnimation::valueChanged, owner,
                     [apply = std::move(apply)](const QVariant& v) { apply(v.toInt()); });
    QObject::connect(anim, &QVariantAnimation::finished, owner, std::move(done));
    anim->start(QAbstractAnimation::DeleteWhenStopped);
    if (anim->state() != QAbstractAnimation::Running) return anim;   // zero-length: already done
    anim->pause();
    auto* tick = new QTimer(anim);
    tick->setTimerType(Qt::PreciseTimer);
    tick->setInterval(support::frameIntervalMs(qobject_cast<QWidget*>(owner)));
    QElapsedTimer clock;
    clock.start();
    QObject::connect(tick, &QTimer::timeout, anim, [anim, clock] {
      if (anim->state() == QAbstractAnimation::Paused)
        anim->setCurrentTime(int(std::min<qint64>(clock.elapsed(), anim->duration())));
    });
    tick->start();
    return anim;
  }

  // Browser appReveal counterpart: window opacity, no per-child effect, so the canvas paint path
  // is untouched.
  inline void fadeInWindow(QWidget* win) {
    auto* fade = new QPropertyAnimation(win, "windowOpacity", win);
    fade->setDuration(support::motionMs(240));
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
  }

}  // namespace stencil::gui
