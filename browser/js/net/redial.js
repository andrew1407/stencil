// Capped exponential backoff for reopening a server's live events feed after an unexpected drop
// (sleep and wake, a network switch, a server restart). Desktop twin: LiveFeed's reconnect.
export const REDIAL_BASE_MS = 1000;
export const REDIAL_CAP_MS = 30_000;

// 1 s, 2 s, 4 s … then every 30 s.
export const redialDelay = (attempt) => Math.min(REDIAL_CAP_MS, REDIAL_BASE_MS * 2 ** attempt);

export class Redial {
  #attempt = 0;
  #timer = null;
  #openedAt = 0;

  constructor(run) { this.run = run; }

  get pending() { return this.#timer !== null; }

  opened() { this.#openedAt = Date.now(); }

  // A feed that stayed up a whole cap was healthy: the next drop starts the ladder again.
  dropped() {
    if (this.#openedAt && Date.now() - this.#openedAt >= REDIAL_CAP_MS) this.#attempt = 0;
    this.#openedAt = 0;
    clearTimeout(this.#timer);
    const ms = redialDelay(this.#attempt++);
    this.#timer = setTimeout(() => { this.#timer = null; this.run(); }, ms);
    return ms;
  }

  cancel() {
    clearTimeout(this.#timer);
    this.#timer = null;
  }
}
