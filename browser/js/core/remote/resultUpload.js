// When the rendered result (a full-resolution PNG, the server's `result` blob) goes up: it trails
// the layout pushes a peer edits from — after the edits go quiet, or after `minGapMs` of steady
// editing — never twice inside `minGapMs`, and at once on flush (project close, explicit save).
export class ResultUploader {
  #run;
  #idleMs;
  #minGapMs;
  #now;
  #setTimer;
  #clearTimer;
  #timer = null;
  #dirtyAt = null;   // first unsent edit (ms); null = nothing to send
  #editAt = 0;    // latest edit (ms)
  #lastAt = -Infinity;

  constructor(run, { idleMs, minGapMs, now = Date.now, setTimer = setTimeout, clearTimer = clearTimeout }) {
    this.#run = run;
    this.#idleMs = idleMs;
    this.#minGapMs = minGapMs;
    // Called bare: a browser's native timer invoked as this object's method throws Illegal invocation.
    this.#now = () => now();
    this.#setTimer = (fn, ms) => setTimer(fn, ms);
    this.#clearTimer = (handle) => clearTimer(handle);
  }

  get pending() { return this.#dirtyAt !== null; }

  // due = max(last + gap, min(quiet since the latest edit, gap since the first unsent one)).
  markDirty() {
    const t = this.#now();
    this.#dirtyAt ??= t;
    this.#editAt = t;
    const due = Math.max(this.#lastAt + this.#minGapMs,
      Math.min(this.#editAt + this.#idleMs, this.#dirtyAt + this.#minGapMs));
    if (this.#timer !== null) this.#clearTimer(this.#timer);
    this.#timer = this.#setTimer(() => { this.#timer = null; this.flush(); }, Math.max(0, due - t));
  }

  flush() {
    if (this.#timer !== null) { this.#clearTimer(this.#timer); this.#timer = null; }
    if (this.#dirtyAt === null) return;
    this.#dirtyAt = null;
    this.#lastAt = this.#now();
    this.#run();
  }

  cancel() {
    if (this.#timer !== null) this.#clearTimer(this.#timer);
    this.#timer = null;
    this.#dirtyAt = null;
  }
}
