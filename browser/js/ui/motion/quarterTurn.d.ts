export interface TurnBox { left: number; top: number; width: number; height: number; }

export interface QuarterTurnOptions {
  /** The flight in ms and its CSS easing (motion.json ROTATE_MS / ROTATE_EASING). */
  ms: number;
  easing: string;
  /** Its overflow is hidden while the box turns. */
  viewport?: HTMLElement | null;
  /** Called when a flight is armed, and when it ends or is cut short. */
  onStart?: () => void;
  onEnd?: () => void;
}

/** The transform that lays box `to` over `from`, turned back a quarter (dir > 0 = CW); null when degenerate. */
export function quarterTurnStart(from: TurnBox, to: TurnBox, dir: number): string | null;

/** Measures `box` before a rotate; `play(dir)` after its relayout animates the turn. No-op without motion. */
export function beginQuarterTurn(box: HTMLElement | null | undefined, opts: QuarterTurnOptions): { play(dir: number): void };
