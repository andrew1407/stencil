// Timers that fire only when a case advances them, for the gesture machines that take an injected
// setTimer / clearTimer. Unlike speech.js stubClock there is no running clock: advance(ms) fires
// every job whose own delay is <= ms, whatever was advanced before.

export const stubTimers = () => {
  const jobs = new Map();
  let seq = 0;
  return {
    setTimer: (fn, ms) => { jobs.set(++seq, { fn, ms }); return seq; },
    clearTimer: (id) => jobs.delete(id),
    get pending() { return jobs.size; },
    advance(ms) {
      for (const [id, job] of [...jobs]) {
        if (job.ms <= ms) { jobs.delete(id); job.fn(); }
      }
    },
  };
};
