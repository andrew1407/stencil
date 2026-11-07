#pragma once

// The window's own motion: a dock's extent slide, the chat's opening extent and the window fade-in.

#include "../../support/vsync/FramePulse.hpp"
#include "../../support/motionPrefs.hpp"   // support::motionMs

#include <QEasingCurve>
#include <QElapsedTimer>
#include <QPointer>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QWidget>
#include <algorithm>
#include <functional>

namespace stencil::gui {

  // The chat dock's extent as it opens: the one it last had, else the browser's default (px).
  inline int chatOpenExtent(int remembered, bool horiz) { return remembered > 80 ? remembered : (horiz ? 345 : 320); }

  // Times the window's paints while an extent slide runs; two in a row past half a frame put the pulse on
  // half rate for the rest of the slide: a full-window raster at 120 Hz dropped frames unevenly (user report).
  class SlidePaintCost : public QObject {
   public:
    SlidePaintCost(QWidget* window, QObject* slide) : QObject(slide), slide(slide) { window->installEventFilter(this); }
   protected:
    bool eventFilter(QObject* o, QEvent* e) override {
      if (e->type() != QEvent::UpdateRequest || held) return false;
      QElapsedTimer clock;
      clock.start();
      o->event(e);
      over = clock.nsecsElapsed() * 2 > qint64(support::FramePulse::frameMs()) * 1000000 ? over + 1 : 0;
      if (over >= 2) {
        held = true;
        support::FramePulse::instance().holdHalfRate(slide);
      }
      return true;
    }
   private:
    QObject* slide;
    int over = 0;
    bool held = false;
  };

  // QMainWindow overrides a child's min/max during its own layout passes, so `apply` pins every frame and `done` releases.
  // One frame per screen refresh, as the browser's slide gets, not Qt's fixed 16ms: held paused and stepped on
  // the app's frame pulse (support/vsync), so a fold's picture and its space repaint as one frame; stop() still cancels it.
  inline QVariantAnimation* startExtentSlide(QObject* owner, int from, int to, int ms,
                                             std::function<void(int)> apply,
                                             std::function<void()> done,
                                             const QEasingCurve& easing = QEasingCurve::OutCubic,
                                             int delayMs = 0) {
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
    QElapsedTimer clock;
    clock.start();
    const int wait = support::motionMs(delayMs);   // `delayMs`: held at `from` that long first
    if (auto* top = qobject_cast<QWidget*>(owner)) new SlidePaintCost(top->window(), anim);
    QPointer<QVariantAnimation> live(anim);
    support::FramePulse::instance().join(anim, [live, clock, wait] {
      if (!live || live->state() != QAbstractAnimation::Paused) return false;
      live->setCurrentTime(int(std::clamp<qint64>(clock.elapsed() - wait, 0, live->duration())));
      return live && live->state() == QAbstractAnimation::Paused;
    });
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
