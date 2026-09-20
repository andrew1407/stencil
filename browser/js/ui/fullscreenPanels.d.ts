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

/** Size both trigger bands to the panels' current state: wide while away, inside the padding once revealed, the top one spanning a shown selection overlay. */
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
