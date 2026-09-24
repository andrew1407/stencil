export declare const PRESS_SLOP_PX: number;
/** `onHold` answers whether the hold acted; only then is the release's click swallowed. */
export declare const wirePressHold: (wrap: Element | null | undefined,
  opts: { holdMs: number; onHold: () => boolean }) => void;
