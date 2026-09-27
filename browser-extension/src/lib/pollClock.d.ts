export declare const POLL_MS: number;

export interface PollClock {
  add(job: () => void): void;
  remove(job: () => void): void;
  stop(): void;
  /** One catch-up run when a tick was skipped while the document was hidden. */
  resume(): void;
  readonly running: boolean;
  readonly size: number;
}

export declare function createPollClock(period?: number, timers?: typeof globalThis,
  isHidden?: () => boolean): PollClock;
/** One module-level instance per document = one timer per open panel. */
export declare const pollClock: PollClock;
