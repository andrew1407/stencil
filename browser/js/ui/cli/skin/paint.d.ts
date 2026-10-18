// Wearing a skin on the CLI stage.
import type { SkinClock } from './table.js';
export interface SkinHost {
  repaint: (frame: number, clock: SkinClock | null) => void;
  repaintBody?: () => void;
  print: (lines: (string | { text: string; indent?: number })[], kind: string) => void;
  setAccent: (hex: string) => void;
  out: HTMLElement;
}
export declare const applySkinVars: (stage: HTMLElement, name: string | null) => void;
/** `off` takes the skin off; `cut` ends its entrance at once. */
export declare const wearSkin: (stage: HTMLElement, name: string | null, host: SkinHost, opts?: {
  now?: () => number; schedule?: (fn: () => void, ms: number) => unknown; cancel?: (t: unknown) => void; random?: () => number;
}) => { off: () => void; cut: () => void };
export declare const GOLD: string;
