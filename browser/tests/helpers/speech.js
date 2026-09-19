// Fakes for the voice-input suites: a scripted SpeechRecognition, a scripted audio
// stack for the level meter, and a controllable clock. Nothing here touches globals.

// A SpeechRecognition whose instances record their calls and let the test fire Chrome's events.
// fire.result takes the CUMULATIVE session list of alternatives lists, each with `isFinal`.
export const createFakeSpeechRecognition = ({ throwOnStart = 0 } = {}) => {
  const instances = [];
  let throwsLeft = throwOnStart;
  class FakeRecognition {
    constructor() {
      this.calls = [];
      this.onstart = this.onresult = this.onerror = this.onend = null;
      instances.push(this);
      const self = this;
      this.fire = {
        start: () => self.onstart?.({}),
        result: (list) => self.onresult?.({
          resultIndex: 0,
          results: list.map(({ text, isFinal = false }) => Object.assign([{ transcript: text }], { isFinal })),
        }),
        end: () => self.onend?.({}),
        error: (error) => self.onerror?.({ error }),
      };
    }
    start() {
      this.calls.push('start');
      if (throwsLeft > 0) {
        throwsLeft--;
        const err = new Error('already started');
        err.name = 'InvalidStateError';
        throw err;
      }
    }
    stop() { this.calls.push('stop'); }
    abort() { this.calls.push('abort'); }
  }
  return { ctor: FakeRecognition, instances, get last() { return instances[instances.length - 1]; } };
};

// getUserMedia / AudioContext / requestAnimationFrame stand-ins. `bytes` is what the
// analyser hands back; `frame(t)` runs the pending animation frame at time t.
export const createFakeAudio = ({ reject = null, suspended = false } = {}) => {
  const streams = [];
  const contexts = [];
  const frames = new Map();
  let seq = 0;
  let bytes = new Uint8Array(256).fill(128);
  const audio = {
    streams, contexts,
    set bytes(b) { bytes = b; },
    get bytes() { return bytes; },
    getUserMedia: async () => {
      if (reject) throw reject;
      const tracks = [{ stopped: false, stop() { this.stopped = true; } }];
      const stream = { tracks, getTracks: () => tracks };
      streams.push(stream);
      return stream;
    },
    AudioContext: class {
      constructor() {
        this.state = suspended ? 'suspended' : 'running';
        this.resumed = 0;
        this.closed = false;
        this.analyser = null;
        this.sources = [];
        contexts.push(this);
      }
      async resume() { this.resumed++; this.state = 'running'; }
      async close() { this.closed = true; }
      createAnalyser() {
        const ctx = this;
        this.analyser = {
          fftSize: 2048,
          getByteTimeDomainData(arr) { arr.set(bytes.subarray(0, arr.length)); ctx.samples = (ctx.samples || 0) + 1; },
        };
        return this.analyser;
      }
      createMediaStreamSource(stream) {
        const src = { stream, connected: null, connect(node) { this.connected = node; } };
        this.sources.push(src);
        return src;
      }
    },
    requestAnimationFrame: (fn) => { frames.set(++seq, fn); return seq; },
    cancelAnimationFrame: (id) => { frames.delete(id); },
    get pendingFrames() { return frames.size; },
    // Run every pending frame once (a frame that re-requests lands in the NEXT call).
    frame() {
      const batch = [...frames.values()];
      frames.clear();
      for (const fn of batch) fn();
    },
  };
  return audio;
};

// A controllable clock: timers fire only when the test advances it; `now()` follows.
export const stubClock = (start = 1000) => {
  const jobs = new Map();
  let seq = 0;
  let t = start;
  return {
    now: () => t,
    setTimer: (fn, ms) => { jobs.set(++seq, { fn, at: t + ms }); return seq; },
    clearTimer: (id) => jobs.delete(id),
    get pending() { return jobs.size; },
    get pendingDelays() { return [...jobs.values()].map((j) => j.at - t); },
    // Move time forward, firing timers in order as their moment passes.
    advance(ms) {
      const end = t + ms;
      for (;;) {
        const due = [...jobs.entries()].filter(([, j]) => j.at <= end).sort((a, b) => a[1].at - b[1].at);
        if (!due.length) break;
        const [id, job] = due[0];
        jobs.delete(id);
        t = Math.max(t, job.at);
        job.fn();
      }
      t = end;
    },
  };
};
