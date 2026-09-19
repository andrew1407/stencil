#pragma once
// Reading a surface flight, for the MainWindow GUI suites: the dust/ghost overlay a
// window, menu or tooltip forms out of, and the point it is aimed at.
#include "../src/support/DisintegrateOverlay.hpp"
#include <QCoreApplication>
#include <QEvent>
#include <QLabel>
#include <QObject>
#include <QPoint>
#include <QElapsedTimer>
#include <QPointer>
#include <QSignalSpy>
#include <QTimer>
#include <QWidget>
#include <QtTest>

namespace stencil::guitest {
  // A surface flight is a DisintegrateOverlay child of the window carrying the point it is aimed at —
  // "where the window comes out of". Q_OBJECT-free, so it is found by object name and cast statically.
  inline stencil::gui::DisintegrateOverlay* surfaceFlight(const QWidget* host) {
    stencil::gui::DisintegrateOverlay* found = nullptr;
    for (QWidget* w : host->findChildren<QWidget*>(
             QString::fromLatin1(stencil::gui::DisintegrateOverlay::OBJECT_NAME))) {
      auto* fx = static_cast<stencil::gui::DisintegrateOverlay*>(w);
      if (fx->surfacePicture().isValid()) found = fx;   // the newest one wins
    }
    return found;
  }

  // Where the live flight is aimed, in `host` coordinates; an invalid point = nothing
  // is flying.
  inline QPoint surfaceFlightTarget(const QWidget* host) {
    auto* fx = surfaceFlight(host);
    return fx ? fx->surfaceTarget() : QPoint(-1, -1);
  }

  // The GHOST twin: the fallback for whatever the dust engine declines (an unmeasurable box, a failed
  // grab). Every dialog dusts first (modalReveal.cpp DIALOG_DUST_MAX_CELLS). Found by name (makeGhost).
  inline QLabel* modalGhost(const QWidget* host) {
    QLabel* found = nullptr;
    for (QLabel* g : host->findChildren<QLabel*>(QStringLiteral("stencilModalGhost")))
      if (g->isVisible()) found = g;   // the newest one wins
    return found;
  }

  // Where either flight mechanism (dust or ghost) STARTS, caught on its QEvent::Show rather than polled:
  // a ghost's geometry is an active tween. Only the FIRST match sticks, so a close flight cannot clobber.
  struct RevealOriginWatcher : QObject {
    QPoint origin{-1, -1};
    bool captured = false;
    void reset() { origin = QPoint(-1, -1); captured = false; }
    bool eventFilter(QObject* o, QEvent* e) override {
      if (captured || e->type() != QEvent::Show) return false;
      auto* w = qobject_cast<QWidget*>(o);
      if (!w) return false;
      if (w->objectName() == QLatin1String(stencil::gui::DisintegrateOverlay::OBJECT_NAME)) {
        auto* fx = static_cast<stencil::gui::DisintegrateOverlay*>(w);
        if (fx->surfacePicture().isValid()) { origin = fx->surfaceTarget(); captured = true; }
      } else if (w->objectName() == QLatin1String("stencilModalGhost")) {
        origin = w->geometry().center();
        captured = true;
      }
      return false;
    }
  };

  // A control's centre in the window's coordinates — what a flight out of it aims at.
  inline QPoint flightPointOf(const QWidget* control, const QWidget* host) {
    return control->mapTo(host, control->rect().center());
  }

  // Wait for the animation held in `slot` on its OWN finished(), which a state that predates the action
  // cannot satisfy — and for any that replaces it. An empty slot means it is over.
  template <class Anim>
  bool awaitAnim(Anim*& slot, int capMs = 3000) {
    QElapsedTimer t;
    t.start();
    while (slot && t.elapsed() < capMs) {
      QSignalSpy done(slot, &QAbstractAnimation::finished);
      if (!done.wait(int(capMs - t.elapsed()))) return false;
    }
    return !slot;
  }

  // A single-shot debounce/idle QTimer announces its own end too: wait for that
  // timeout() rather than for a span guessed longer than it. Not armed = already past.
  inline bool awaitTimer(QTimer* t, int capMs = 3000) {
    if (!t || !t->isActive()) return true;
    QSignalSpy fired(t, &QTimer::timeout);
    return fired.wait(capMs);
  }

  // The dust has no such signal: DisintegrateOverlay is Q_OBJECT-free and retires itself with
  // deleteLater, so its absence is the only completion there is — this polls, and flushes the deletes.
  inline bool awaitFlights(QWidget* host, int capMs = 3000) {
    return QTest::qWaitFor([host] {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      return surfaceFlight(host) == nullptr;
    }, capMs);
  }

  // Walk a popup's highlight with `key` until `done` holds. QMenu moves the highlight inside its own key
  // handler, so a step that lands needs no wait at all; one that does not gets `stepMs` to catch up.
  template <class F>
  bool walkMenu(QWidget* popup, Qt::Key key, F done, int steps = 40, int stepMs = 20) {
    for (int i = 0; i < steps && !done(); ++i) {
      QTest::keyClick(popup, key);
      (void)QTest::qWaitFor([&] { return done(); }, stepMs);
    }
    return done();
  }

  // Every visible rect under `w` — the signature a relayout changes.
  inline QList<QRect> layoutSig(QWidget* w) {
    QList<QRect> r;
    for (QWidget* c : w->findChildren<QWidget*>())
      if (c->isVisible()) r.append(c->geometry());
    return r;
  }

  // A relayout announces nothing, so poll that signature until it holds still for three
  // looks. Capped at the fixed wait it replaces: a slow pass still gets the time it had.
  inline void settleLayout(QWidget* w, int capMs) {
    QElapsedTimer t;
    t.start();
    QList<QRect> a = layoutSig(w);
    for (int held = 0; t.elapsed() < capMs;) {
      QTest::qWait(16);
      const QList<QRect> b = layoutSig(w);
      held = (b == a) ? held + 1 : 0;
      a = b;
      if (held >= 3) return;
    }
  }
}  // namespace stencil::guitest

// QTRY looks every 50 ms, so a condition that lands in 5 costs 50 — across this suite
// that was seconds of pure overshoot. Same timeout, same assertion, finer look.
#undef QTRY_IMPL
#define QTRY_IMPL(expr, timeoutAsGiven) \
    const auto qt_test_timeoutAsMs = [&] { \
            using namespace std::chrono_literals; \
            return std::chrono::milliseconds{timeoutAsGiven}; \
        }(); \
    const auto qt_test_step = qt_test_timeoutAsMs < std::chrono::milliseconds(35) \
                              ? qt_test_timeoutAsMs / 7 + std::chrono::milliseconds(1) \
                              : std::chrono::milliseconds(5); \
    { QTRY_LOOP_IMPL(expr, qt_test_timeoutAsMs, qt_test_step) } \
    QTRY_TIMEOUT_DEBUG_IMPL(expr, qt_test_timeoutAsMs, qt_test_step)

