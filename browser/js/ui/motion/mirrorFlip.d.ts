export interface MirrorFlipOptions {
  /** The flight in ms and its CSS easing (motion.json ROTATE_MS / ROTATE_EASING). */
  ms: number;
  easing: string;
  /** Called when a flight is armed, and when it ends or is cut short. */
  onStart?: () => void;
  onEnd?: () => void;
}

/** Arms a left-right flip of `box`; `play()` after the rebuild turns it over. No-op without motion. */
export function beginMirrorFlip(box: HTMLElement | null | undefined, opts: MirrorFlipOptions): { play(): void };
