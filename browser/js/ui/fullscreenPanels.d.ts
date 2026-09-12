export interface FsPanels {
  showControlsPanel(): void;
  hideControlsPanel(): void;
  showPointsPanel(): void;
  hidePointsPanel(): void;
  /** Cancel a pending controls auto-hide (hovering the strip, or a drag). */
  pauseControlsHide(): void;
  /** Cancel a pending points auto-hide (hovering the panel or its resizer). */
  pausePointsHide(): void;
}

/** The two slide-in panels and their auto-hide timers. `showPoints` re-clones the list. */
export declare function createFsPanels(deps: {
  fsControlsPanel: HTMLElement;
  fsPointsPanel: HTMLElement;
  showPoints: () => void;
}): FsPanels;
