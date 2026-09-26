// The Alt-peek gesture (support/tip/altPeek, browser twin ui/tip/popover.js
// createModalOpenGesture) over fake windows: peek, release outside / inside, the linger close
// on leave, the glide registry, and a deliberate open that Alt never touches.
#include "altPeek.hpp"

#include <QTest>
#include <QWidget>

#include "../check.hpp"

using stencil::support::AltPeekGesture;

namespace {
  struct Fake {
    bool open = false;
    bool engaged = false;
    AltPeekGesture* g = nullptr;
    Fake(QObject* parent) {
      g = new AltPeekGesture({[this] { open = true; }, [this] { open = false; },
                              [this] { return open; }, [this] { return engaged; }},
                             parent);
    }
  };
}  // namespace

void runAltPeekGestureChecks() {
  QObject host;
  Fake a(&host), b(&host);
  using M = AltPeekGesture::Mode;

  a.g->altHover(nullptr);
  check(a.open && a.g->mode() == M::PEEK, "gesture: Alt+hover opens a peek");
  a.g->altRelease();
  check(!a.open && a.g->mode() == M::NONE, "gesture: Alt released outside the list closes it");

  a.g->altHover(nullptr);
  a.engaged = true;
  a.g->altRelease();
  check(a.open && a.g->mode() == M::LINGER, "gesture: released over the list, it lingers");
  a.g->boxLeave();
  a.g->boxEnter();
  QTest::qWait(stencil::support::LINGER_CLOSE_MS + 100);
  check(a.open, "gesture: coming back in before the linger elapses keeps it open");
  a.g->boxLeave();
  QTest::qWait(stencil::support::LINGER_CLOSE_MS / 2);
  check(a.open, "gesture: the linger close waits LINGER_CLOSE_MS");
  QTest::qWait(stencil::support::LINGER_CLOSE_MS);
  check(!a.open && a.g->mode() == M::NONE, "gesture: …then leaving the list closes it");
  a.engaged = false;

  a.g->altHover(nullptr);
  b.g->altHover(nullptr);
  check(!a.open && b.open, "gesture: gliding onto another opener closes the previous peek");
  b.g->altRelease();

  a.open = true;   // opened by a click: the gesture never adopted it
  a.g->altHover(nullptr);
  a.g->altRelease();
  check(a.open, "gesture: Alt hover and release leave a click-opened list alone");
  b.g->altHover(nullptr);
  check(a.open, "gesture: …and a glide does not close it");
  b.g->altRelease();

  bool closedByGlide = false;
  QObject other;
  stencil::support::addGlideHandle(&other, [&](QWidget*) { closedByGlide = true; });
  a.open = false;
  a.g->altHover(nullptr);
  check(closedByGlide, "gesture: a peek opening reaches every other registered handle");
  a.g->notifyClosed();
  a.open = false;
  a.g->altRelease();
  check(a.g->mode() == M::NONE, "gesture: notifyClosed drops the peek state");
}
