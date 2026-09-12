// MV3 panels are short-lived, so the shared-pin and editor-preview refreshes poll; they
// share ONE interval, since two timers on the same tick cost twice the wakeups.
export const POLL_MS = 8000;

export const createPollClock = (period = POLL_MS, timers = globalThis) => {
  const jobs = new Set();
  let timer = null;
  const sync = () => {
    if (jobs.size && timer === null) timer = timers.setInterval(() => { for (const job of [...jobs]) job(); }, period);
    else if (!jobs.size && timer !== null) { timers.clearInterval(timer); timer = null; }
  };
  return {
    add: (job) => { jobs.add(job); sync(); },
    remove: (job) => { jobs.delete(job); sync(); },
    stop: () => { jobs.clear(); sync(); },
    get running() { return timer !== null; },
    get size() { return jobs.size; },
  };
};

// One module-level instance per document = one timer per open panel.
export const pollClock = createPollClock();
