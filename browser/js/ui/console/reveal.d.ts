// The reveal sweep's pace: how new console output types in.
export interface RevealPace { stepMs: number; quietMs: number; burstMaxMs: number; instant: boolean; }
export interface RevealPlan { jumps: number; stepMs: number; }
export declare const pace: (speed: number) => RevealPace;
export declare const createReveal: (opts?: { speed?: () => number }) => {
  plan: (nowMs: number) => RevealPlan;
  /** A sweep ended now: the quiet window counts from its end. */
  done: (now: number) => void;
  reset: () => void;
};
