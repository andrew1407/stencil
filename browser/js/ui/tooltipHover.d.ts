import type { StencilTooltip } from './tooltip.js';

export interface HoverMods {
  altKey?: boolean;
  ctrlKey?: boolean;
  metaKey?: boolean;
  shiftKey?: boolean;
}

/** Picks the target under the cursor and asks the tooltip to reveal it (or hides it). */
export declare function decideHover(
  tip: StencilTooltip,
  clientX: number,
  clientY: number,
  x: number,
  y: number,
  mods: HoverMods,
  immediate?: boolean,
): void;

/** Re-decides at the last known cursor position, without the reveal delay. */
export declare function refreshHover(tip: StencilTooltip, mods: HoverMods): void;

/** Arms the wake-up delay for `key`; the same key re-runs at once, a new one re-arms. */
export declare function scheduleReveal(
  tip: StencilTooltip,
  key: string,
  revealFn: () => void,
  immediate: boolean,
): void;
