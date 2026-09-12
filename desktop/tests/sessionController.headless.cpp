// Headless check of app/sessionController.hpp — the rules that decide whether the
// editor writes anything at all (incognito, a fetched server project with sync off,
// no project, no image, a restore still in flight) and the two debounces that carry
// the writes. These gates used to be spelled out at four call sites; this is the pin
// that keeps them from drifting apart again. Needs QtCore for the timers, no display.
#include "sessionController.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include "support/check.hpp"

using stencil::gui::SessionController;
using Gates = SessionController::Gates;

// A normal, saveable editor: local project, picture on screen, not incognito.
static Gates ready() { return {false, false, true, true}; }

static void pump(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // ── Session writes: incognito and the in-memory-only server project are the vetoes.
  check(SessionController::wantsSessionWrite(ready()), "an ordinary editor persists its session");
  {
    Gates g = ready();
    g.incognito = true;
    check(!SessionController::wantsSessionWrite(g), "an incognito editor never writes its session");
  }
  {
    Gates g = ready();
    g.remoteUnsynced = true;
    check(!SessionController::wantsSessionWrite(g),
          "a fetched server project with sync off is stored nowhere");
  }
  {
    Gates g = ready();
    g.hasActiveProject = false;
    g.hasImage = false;
    check(SessionController::wantsSessionWrite(g),
          "an unsaved scratch editor still writes the restore blob");
  }

  // ── Scheduling a view save: the same vetoes, plus "there is a view to save".
  check(SessionController::wantsViewSchedule(ready(), false), "a moved view is worth debouncing");
  check(!SessionController::wantsViewSchedule(ready(), true),
        "a restore in flight does not re-schedule the view it is applying");
  {
    Gates g = ready();
    g.hasActiveProject = false;
    check(!SessionController::wantsViewSchedule(g, false), "no project, nowhere to put the view");
    g = ready();
    g.hasImage = false;
    check(!SessionController::wantsViewSchedule(g, false), "no picture, no pan or zoom to record");
    g = ready();
    g.incognito = true;
    check(!SessionController::wantsViewSchedule(g, false), "incognito keeps its view to itself");
  }

  // ── Writing the view: hasImage is NOT re-checked (the debounce may outlive the
  // picture), but the remote-unsynced veto is — it is not asked when scheduling.
  check(SessionController::wantsViewWrite(ready(), false), "the debounce writes the view");
  check(!SessionController::wantsViewWrite(ready(), true), "…unless a restore is still running");
  {
    Gates g = ready();
    g.remoteUnsynced = true;
    check(!SessionController::wantsViewWrite(g, false),
          "an in-memory server project has no local row to update");
    g = ready();
    g.hasImage = false;
    check(SessionController::wantsViewWrite(g, false),
          "a picture closed mid-debounce still flushes the pending view");
  }

  // ── The no-op test that keeps a layout-induced scrollbar signal from rewriting the
  // store (and flashing "Saved") when nothing actually moved.
  check(!SessionController::viewMoved(1.0, 10, 20, 1.0, 10, 20), "an unchanged view is not a save");
  check(SessionController::viewMoved(1.5, 10, 20, 1.0, 10, 20), "a zoom change is");
  check(SessionController::viewMoved(1.0, 11, 20, 1.0, 10, 20), "so is a horizontal pan");
  check(SessionController::viewMoved(1.0, 10, 21, 1.0, 10, 20), "so is a vertical one");

  // ── The restore guard is plain, sticky state: set around the apply, cleared after.
  {
    SessionController sc;
    check(!sc.restoring(), "a fresh controller is not restoring");
    sc.setRestoring(true);
    check(sc.restoring() && !SessionController::wantsViewSchedule(ready(), sc.restoring()),
          "while restoring, nothing schedules");
    sc.setRestoring(false);
    check(!sc.restoring(), "and the guard lifts again");
  }

  // ── The debounces themselves: single-shot, coalescing, and gated by the same rules.
  {
    SessionController sc;
    int sessions = 0, views = 0;
    sc.attach(&app, [&] { ++sessions; }, [&] { ++views; });
    for (int i = 0; i < 5; ++i) {
      sc.scheduleAutosave(true, ready());
      sc.scheduleViewSave(ready());
    }
    check(sessions == 0 && views == 0, "neither save runs synchronously");
    pump(SessionController::AUTOSAVE_MS + 250);
    check(sessions == 1, "five edits in a burst coalesce into one session write");
    check(views == 1, "…and one view write");
  }
  {
    SessionController sc;
    int sessions = 0, views = 0;
    sc.attach(&app, [&] { ++sessions; }, [&] { ++views; });
    Gates off = ready();
    off.incognito = true;
    sc.scheduleAutosave(true, off);
    sc.scheduleViewSave(off);
    sc.scheduleAutosave(false, ready());   // autosave turned off in Settings
    pump(SessionController::AUTOSAVE_MS + 250);
    check(sessions == 0 && views == 0, "a vetoed schedule arms no timer at all");
  }
  // A server project that gains sync mid-debounce still writes: the remote veto is
  // asked when the autosave fires, not when it is armed.
  {
    SessionController sc;
    int sessions = 0;
    sc.attach(&app, [&] { ++sessions; }, [] {});
    Gates g = ready();
    g.remoteUnsynced = true;
    sc.scheduleAutosave(true, g);
    pump(SessionController::AUTOSAVE_MS + 250);
    check(sessions == 1, "the autosave timer arms regardless of the remote sync setting");
  }

  std::printf(failures ? "\nFAILED (%d)\n" : "\nOK\n", failures);
  return failures ? 1 : 0;
}
