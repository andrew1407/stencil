export interface FsPanels {
  showControlsPanel(): void;
  hideControlsPanel(): void;
  showPointsPanel(): void;
  hidePointsPanel(): void;
  /** Cancel a pending controls auto-hide (hovering the strip, or a drag); a strip still dissolving comes back. */
  pauseControlsHide(): void;
  /** Cancel a pending points auto-hide (hovering the panel or its resizer); a list still dissolving comes back. */
  pausePointsHide(): void;
  /** Leaving fullscreen: timers cleared, clouds settled, both panels hidden at once. */
  reset(): void;
}

/** The points list's gather clock; the stylesheet's handle fade rides the same number. */
export declare const POINTS_DUST_IN_MS: number;

/** The hover band that reveals a hidden panel, in px (the desktop's PANEL_REVEAL_PX). */
export declare const FS_TRIGGER_PX: number;

/** The canvas's box less a chat docked over its left or right side; null when either is unmeasured. */
export declare const canvasSpan: (viewport: Element | null | undefined, chat: Element | null | undefined) =>
  { left: number; right: number; top: number; width: number; height: number } | null;

/**
 * Size both trigger bands to the panels' current state: wide while away, inside the padding once
 * revealed, the top one spanning a shown selection overlay; and lay the strip, that overlay, the
 * points list and its handle along the canvas's own columns, clear of a chat docked beside it.
 */
export declare function syncFsTriggers(): void;

/** A panel's dust past its own edge; true while the motes fly, false hands the reveal to the CSS slide. */
export declare function panelDust(panel: HTMLElement, dock: 'top' | 'right', hiding: boolean, inMs?: number): boolean;

/** The two slide-in panels and their auto-hide timers. `showPoints` re-clones the list; `dust` is panelDust unless a test supplies its own. */
export declare function createFsPanels(deps: {
  fsControlsPanel: HTMLElement;
  fsPointsPanel: HTMLElement;
  showPoints: () => void;
  dust?: typeof panelDust;
}): FsPanels;
