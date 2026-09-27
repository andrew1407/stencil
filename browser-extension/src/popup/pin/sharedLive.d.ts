// Shapes for popup/pin/sharedLive.js — server feeds first, the panel clock as the fallback.
import type { PollClock } from '../../lib/pollClock.js';
import type { EventsSubscription, ProjectEvent } from '../../lib/connection/events.js';

export declare const MAX_STRIDE: number;
export declare const EVENT_DEBOUNCE_MS: number;

export interface SharedLive {
  start(): void;
  stop(): void;
  /** The poll job the clock runs. */
  tick(): Promise<void>;
  readonly liveFeeds: number;
  readonly stride: number;
}

export declare const createSharedLive: (opts: {
  refresh: () => Promise<boolean>;
  getConnections: () => Array<{ url: string; token: string }>;
  clock?: PollClock;
  subscribe?: (conn: { url: string; token: string }, onEvent: (msg: ProjectEvent) => void,
    opts: { onState: (live: boolean) => void }) => EventsSubscription | null;
  timers?: typeof globalThis;
}) => SharedLive;
