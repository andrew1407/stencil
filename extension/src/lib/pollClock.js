// ── One poll-while-open heartbeat ───────────────────────────────────────────
// MV3 panels are short-lived, so the shared-pin refresh (popup.js) and the editor preview
// refresh (editorMode.js) poll instead of holding a background channel. Both want the same
// period, so they share ONE interval here: two timers on the same tick cost twice the wakeups
// and interleave their round-trips. Jobs are plain callbacks; the timer exists only while at
// least one is registered.
export const POLL_MS = 8000;

export const createPollClock = (period = POLL_MS, timers = globalThis) => {
  const jobs = new Set();
  let timer = null;
  const sync = () => {
    if (jobs.size && timer === null) timer = timers.setInterval(() => { for (const job of [...jobs]) job(); }, period);
    else if (!jobs.size && timer !== null) { timers.clearInterval(timer); timer = null; }
  };
  return {
    // Idempotent: registering the same job twice still ticks it once.
    add: (job) => { jobs.add(job); sync(); },
    remove: (job) => { jobs.delete(job); sync(); },
    stop: () => { jobs.clear(); sync(); },
    get running() { return timer !== null; },
    get size() { return jobs.size; },
  };
};

// The panel's clock. Every surface that runs popup.js gets its own document, so one
// module-level instance per surface is exactly one timer per open panel.
export const pollClock = createPollClock();
