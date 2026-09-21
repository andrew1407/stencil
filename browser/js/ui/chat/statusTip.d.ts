import type { DrawingApp } from '../../core/drawingApp.js';

export interface ChatStatusTip {
  /** Probe the configured provider and repaint the dot; a mid-probe ask re-runs once. */
  refreshStatus: () => Promise<void>;
  /** Take the tooltip down — the "…" menu opens into the same corner. */
  hideGearTip: () => void;
}

/** The provider status dot on the "…" trigger, and the table it shows on hover. */
export declare function createChatStatusTip(deps: {
  app: DrawingApp;
  statusDot: HTMLElement;
  statusHost: HTMLElement;
}): ChatStatusTip;
