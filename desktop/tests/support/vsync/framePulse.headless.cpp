// Headless check of the display-synced frame clock (src/support/vsync/): steps joined to the pulse run in
// the same beat, EARLY before LATE; a finished or orphaned step leaves it and it idles empty; the ticker
// starts, stops and restarts like a QTimer; and Qt's animations advance on it once the driver is installed.
#include "../../../src/support/vsync/AnimationDriver.hpp"
#include "../../../src/support/vsync/FramePulse.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QVariantAnimation>
#include <memory>
#include <vector>

using namespace stencil::support;

#include "../../support/check.hpp"

static void pumpUntil(const std::function<bool()>& done, int capMs = 2000) {
  QElapsedTimer t;
  t.start();
  while (!done() && t.elapsed() < capMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  FramePulse& pulse = FramePulse::instance();
  check(&pulse == &FramePulse::instance(), "one pulse per app");

  // ── Order and lockstep: a LATE step joined first still runs after an EARLY one, in the same beat.
  QObject ghost, space;
  std::vector<char> seen;
  int lateRuns = 0, earlyRuns = 0;
  pulse.join(&ghost, [&] { seen.push_back('L'); return ++lateRuns < 3; }, FramePulse::LATE);
  pulse.join(&space, [&] { seen.push_back('E'); return ++earlyRuns < 3; });
  pumpUntil([&] { return pulse.size() == 0; });
  check(seen == std::vector<char>({'E', 'L', 'E', 'L', 'E', 'L'}), "each beat steps the space, then the ghost");
  check(pulse.size() == 0, "finished steps leave the pulse");

  // ── An owner gone mid-flight drops its step without running it again.
  auto owner = std::make_unique<QObject>();
  int orphanRuns = 0;
  pulse.join(owner.get(), [&] { ++orphanRuns; return true; });
  pumpUntil([&] { return orphanRuns >= 1; });
  owner.reset();
  const int before = orphanRuns;
  pumpUntil([&] { return pulse.size() == 0; });
  check(orphanRuns == before && pulse.size() == 0, "a destroyed owner's step leaves the pulse");

  // ── A step may join another mid-beat; the newcomer runs from the next beat on.
  QObject a, b;
  int bRuns = 0;
  bool joined = false;
  pulse.join(&a, [&] {
    if (!joined) { joined = true; pulse.join(&b, [&] { return ++bRuns < 2; }); }
    return false;
  });
  pumpUntil([&] { return pulse.size() == 0; });
  check(bRuns == 2, "a step joined from inside a beat runs to its end");

  // ── The ticker: a QTimer's start/stop over the pulse, one step however often it restarts.
  int ticks = 0;
  auto* ticker = new FrameTicker(&app, [&] { ++ticks; });
  ticker->start();
  ticker->stop();
  ticker->start();
  check(ticker->isActive() && pulse.size() == 1, "a restart between beats keeps one step");
  pumpUntil([&] { return ticks >= 3; });
  ticker->stop();
  const int stopped = ticks;
  pumpUntil([&] { return pulse.size() == 0; });
  check(ticks == stopped && pulse.size() == 0, "a stopped ticker leaves the pulse");
  ticker->start();
  pumpUntil([&] { return ticks > stopped; });
  check(ticks > stopped, "a stopped ticker starts again");
  delete ticker;
  pumpUntil([&] { return pulse.size() == 0; });
  check(pulse.size() == 0, "a deleted ticker leaves the pulse");

  // ── Qt's animations on the pulse: they advance, finish, and the driver leaves it once idle.
  AnimationDriver::installForApp();
  AnimationDriver::installForApp();
  QVariantAnimation anim;
  anim.setDuration(80);
  anim.setStartValue(0.0);
  anim.setEndValue(1.0);
  int steps = 0;
  QObject::connect(&anim, &QVariantAnimation::valueChanged, [&] { ++steps; });
  anim.start();   // Qt starts its driver a turn later
  pumpUntil([&] { return pulse.size() == 1 || anim.state() == QAbstractAnimation::Stopped; });
  check(pulse.size() == 1 && anim.state() == QAbstractAnimation::Running, "a running animation puts the driver on the pulse");
  pumpUntil([&] { return anim.state() == QAbstractAnimation::Stopped; });
  check(anim.state() == QAbstractAnimation::Stopped && anim.currentValue().toDouble() == 1.0,
        "an animation driven by the pulse lands on its end value");
  check(steps >= 3, "the animation advanced over several beats");
  pumpUntil([&] { return pulse.size() == 0; });
  check(pulse.size() == 0, "the driver leaves the pulse once no animation runs");

  return failures ? 1 : 0;
}
