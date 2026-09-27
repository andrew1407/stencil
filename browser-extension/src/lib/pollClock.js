// MV3 panels are short-lived, so the shared-pin and editor-preview refreshes poll; they
// share ONE interval, since two timers on the same tick cost twice the wakeups. A hidden
// panel skips its ticks and runs once the moment it shows again.
export const POLL_MS = 8000;

const pageHidden = () => globalThis.document?.visibilityState === 'hidden';

export const createPollClock = (period = POLL_MS, timers = globalThis, isHidden = pageHidden) => {
  const jobs = new Set();
  let timer = null;
  let missed = false;
  const runAll = () => { for (const job of [...jobs]) job(); };
  const tick = () => {
    if (isHidden()) { missed = true; return; }
    runAll();
  };
  const sync = () => {
    if (jobs.size && timer === null) timer = timers.setInterval(tick, period);
    else if (!jobs.size && timer !== null) { timers.clearInterval(timer); timer = null; missed = false; }
  };
  return {
    add: (job) => { jobs.add(job); sync(); },
    remove: (job) => { jobs.delete(job); sync(); },
    stop: () => { jobs.clear(); sync(); },
    resume: () => { if (missed && jobs.size) { missed = false; runAll(); } },
    get running() { return timer !== null; },
    get size() { return jobs.size; },
  };
};

// One module-level instance per document = one timer per open panel.
export const pollClock = createPollClock();
if (typeof globalThis.document?.addEventListener === 'function') {
  globalThis.document.addEventListener('visibilitychange', () => { if (!pageHidden()) pollClock.resume(); });
}
