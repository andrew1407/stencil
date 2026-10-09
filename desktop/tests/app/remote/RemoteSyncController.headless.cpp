// Headless check of app/RemoteSyncController — every guard a peer change has to get past
// before it swaps the canvas (the op-plan gate, the reload-in-flight gate, the coalescing
// reload timer), the trailing push debounce and the throttled result upload. QtCore timers
// only, no display, no server.
#include "RemoteSyncController.hpp"
#include "RemoteSession.hpp"
#include "ServerClient.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>
#include <utility>

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
  int reloads = 0, pushes = 0, detaches = 0, uploads = 0;
  std::function<void()> finishUpload;
  RemoteSyncController ctrl(
      nullptr, &session, &reloading, &pushing, &planRunning,
      RemoteSyncController::Hooks{
          [&syncOn] { return syncOn; },
          [&incognito] { return incognito; },
          [&pushes] { ++pushes; },
          [&reloads](const QString&, const QString&, bool) { ++reloads; },
          [&detaches] { ++detaches; },
          [&uploads, &finishUpload](std::function<void()> done) {
            ++uploads;
            finishUpload = std::move(done);
          },
      });
  ctrl.setResultTiming(60, 400);

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

  // A push in flight owns the link's version: the next one waits for it, then leaves once.
  pushing = true;
  ctrl.scheduleRemotePush();
  pump(800);
  check(pushes == 0, "no push starts while another is in flight");
  pushing = false;
  ctrl.pushFinished();
  pump(300);
  check(pushes == 1, "the waiting push leaves once the one in flight has settled");
  pushes = 0;
  incognito = true;
  ctrl.scheduleRemotePush();
  pump(700);
  check(pushes == 0, "an incognito editor pushes nothing");
  incognito = false;

  // The baked result: once per burst of pushes, off the edit, never twice within the gap.
  check(!ctrl.flushResultUpload(), "nothing to flush before any push committed");
  for (int i = 0; i < 3; ++i) ctrl.scheduleResultUpload();
  check(uploads == 0, "the result never renders inside the push that staled it");
  pump(150);
  check(uploads == 1, "a burst of committed pushes bakes one result once the edits idle");
  pushes = 0;
  ctrl.scheduleRemotePush();
  pump(500);
  check(pushes == 0, "a push waits while our own result upload is bumping the version");
  // Our upload's own version echo arrives while it is in flight; the re-read adopts it.
  reloads = 0;
  ctrl.onRemoteProjectEvent(ID, 10, false);
  session.getLink().version = 10;
  std::exchange(finishUpload, {})();
  pump(200);
  check(pushes == 1, "the held push leaves once the upload has landed");
  check(reloads == 0, "our own upload's echo never reloads the canvas");
  ctrl.scheduleResultUpload();
  pump(150);
  check(uploads == 2, "a result long after the last one bakes once the edits idle");
  std::exchange(finishUpload, {})();
  ctrl.scheduleResultUpload();
  pump(150);
  check(uploads == 2, "a result staled right after one landed waits out the gap");
  pump(400);
  check(uploads == 3, "…and then bakes again");
  bool settled = false;
  check(ctrl.flushResultUpload([&settled] { settled = true; }),
        "a flush reports the upload still in flight");
  check(!settled, "…and has not settled while it is");
  std::exchange(finishUpload, {})();
  check(settled && !ctrl.resultBusy(), "the flush settles once the last result has landed");

  // Leaving the project clears an upload still in flight; its late answer changes nothing.
  ctrl.scheduleResultUpload();
  pump(450);
  check(uploads == 4 && ctrl.resultBusy(), "a fourth result is in flight");
  ctrl.stopRemotePoll();
  check(!ctrl.resultBusy(), "leaving the project clears the result in flight");
  std::exchange(finishUpload, {})();
  check(!ctrl.resultBusy(), "…and the left project's late answer leaves it clear");

  // The guarded write behind every push lets go of its callbacks once done: they hold the
  // push guard, and a flag that never clears stops every later poll and reload.
  {
    typedef stencil::net::ServerClient::GuardOutcome GO;
    auto held = std::make_shared<int>(0);
    const std::weak_ptr<int> watch = held;
    int tries = 0;
    stencil::net::ServerClient::runGuardedWriteAsync(
        3, 1,
        [held, &tries](qint64, std::function<void(GO)> cb) { cb(++tries < 2 ? GO::CONFLICT : GO::COMMITTED); },
        [held](qint64, std::function<void(bool, qint64)> cb) { cb(true, 2); },
        [held](GO) {});
    held.reset();
    check(tries == 2 && watch.expired(), "a finished guarded write releases what its callbacks hold");
  }

  // The timings are the browser's, from constants.json; the qrc-less fallbacks match them.
  check(RemoteSyncController::tableMs("COEDIT", "resultIdleMs", -1) == RemoteSyncController::RESULT_IDLE_MS &&
            RemoteSyncController::tableMs("COEDIT", "resultMinGapMs", -1) == RemoteSyncController::RESULT_GAP_MS &&
            RemoteSyncController::tableMs("POLL", "remoteMs", -1) == RemoteSyncController::POLL_MS,
        "the co-edit timings are constants.json COEDIT and POLL");

  // A delete detaches at once, sync toggle or not — so it goes last: it stops the timers.
  syncOn = false;
  ctrl.onRemoteProjectEvent(ID, 9, true);
  check(detaches == 1, "a deleted project detaches the editor whatever the sync toggle says");

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
