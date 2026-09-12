export declare const POLL_MS: number;

export interface PollClock {
  add(job: () => void): void;
  remove(job: () => void): void;
  stop(): void;
  readonly running: boolean;
  readonly size: number;
}

export declare function createPollClock(period?: number, timers?: typeof globalThis): PollClock;
/** One module-level instance per document = one timer per open panel. */
export declare const pollClock: PollClock;
