// Capped exponential backoff for reopening a server's live events feed after an unexpected drop
// (sleep and wake, a network switch, a server restart). Desktop twin: LiveFeed's reconnect.

/** The first retry's delay, ms. */
export declare const REDIAL_BASE_MS: number;
/** The longest delay between retries, ms; a feed open this long resets the ladder. */
export declare const REDIAL_CAP_MS: number;
/** The delay before retry `attempt` (0-based), ms: doubling from REDIAL_BASE_MS, capped. */
export declare const redialDelay: (attempt: number) => number;

export declare class Redial {
  constructor(run: () => void);
  run: () => void;
  /** A retry is scheduled and has not run yet. */
  readonly pending: boolean;
  /** The feed opened: a drop after REDIAL_CAP_MS of uptime restarts the ladder. */
  opened(): void;
  /** The feed dropped: schedules `run` after the next delay, and returns that delay in ms. */
  dropped(): number;
  cancel(): void;
}
