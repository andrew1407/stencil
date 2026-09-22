// Headless check of app/RemoteSyncController — every guard a peer change has to get past
// before it swaps the canvas: the op-plan gate, the reload-in-flight gate, the coalescing
// reload timer and the trailing push debounce. QtCore timers only, no display, no server.
#include "RemoteSyncController.hpp"
#include "RemoteSession.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QString>
#include <QTimer>

#include "../../support/check.hpp"

using stencil::gui::RemoteSession;
using stencil::gui::RemoteSyncController;

static void pump(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  const QString ADDR = QStringLiteral("http://127.0.0.1:1");
  const QString ID = QStringLiteral("p1");
  RemoteSession session(nullptr, nullptr);
  session.getLink().bind(ADDR, ID, QStringLiteral("Project"), QString(), 1);

  bool reloading = false, pushing = false, planRunning = false;
  bool syncOn = true, incognito = false;
  int reloads = 0, pushes = 0, detaches = 0;
  RemoteSyncController ctrl(
      nullptr, &session, &reloading, &pushing, &planRunning,
      RemoteSyncController::Hooks{
          [&syncOn] { return syncOn; },
          [&incognito] { return incognito; },
          [&pushes] { ++pushes; },
          [&reloads](const QString&, const QString&, bool) { ++reloads; },
          [&detaches] { ++detaches; },
      });

  // A peer version ahead of ours reloads, but off the timer, never from the event slot.
  ctrl.onRemoteProjectEvent(ID, 2, false);
  check(reloads == 0, "the reload never runs inside the event that asked for it");
  pump(250);
  check(reloads == 1, "a peer version ahead of ours pulls the project back in");

  reloads = 0;
  for (qint64 v = 3; v <= 6; ++v) ctrl.onRemoteProjectEvent(ID, v, false);
  pump(250);
  check(reloads == 1, "a burst of peer events coalesces into one reload");

  reloads = 0;
  ctrl.onRemoteProjectEvent(ID, 1, false);
  pump(150);
  check(reloads == 0, "a version at or behind ours is our own echo, not a peer change");
  ctrl.onRemoteProjectEvent(QStringLiteral("other"), 9, false);
  pump(150);
  check(reloads == 0, "an event for another project is not ours to reload");
  syncOn = false;
  ctrl.onRemoteProjectEvent(ID, 9, false);
  pump(150);
  check(reloads == 0, "with sync off a peer change never reaches the canvas");
  syncOn = true;

  // The gate: an op plan owns the canvas for its whole run, and the change still lands after.
  reloads = 0;
  planRunning = true;
  ctrl.onRemoteProjectEvent(ID, 7, false);
  pump(350);
  check(reloads == 0, "a peer change waits while an op plan holds the canvas");
  planRunning = false;
  pump(350);
  check(reloads == 1, "the held peer change lands once the plan is done");

  reloads = 0;
  reloading = true;
  ctrl.onRemoteProjectEvent(ID, 8, false);
  pump(350);
  check(reloads == 0, "a reload already in flight is not started a second time");
  reloading = false;
  pump(350);
  check(reloads == 1, "the queued reload runs when the one in flight clears");

  // The push debounce: trailing, and vetoed by incognito.
  ctrl.scheduleRemotePush();
  check(pushes == 0, "the push never leaves from the edit itself");
  pump(700);
  check(pushes == 1, "an edit flushes to the server once the burst settles");
  pushes = 0;
  incognito = true;
  ctrl.scheduleRemotePush();
  pump(700);
  check(pushes == 0, "an incognito editor pushes nothing");
  incognito = false;

  // A delete detaches at once, sync toggle or not — so it goes last: it stops the timers.
  syncOn = false;
  ctrl.onRemoteProjectEvent(ID, 9, true);
  check(detaches == 1, "a deleted project detaches the editor whatever the sync toggle says");

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
