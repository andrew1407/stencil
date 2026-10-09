import type { DrawingApp } from '../../core/drawingApp.js';

export interface ChatStatusTip {
  /** Probe the configured provider and repaint the dot; a mid-probe ask re-runs once. */
  refreshStatus: () => Promise<void>;
  /** Take the tooltip down — the "…" menu opens into the same corner. */
  hideGearTip: () => void;
}

/** The status table a trigger shows on hover / keyboard focus; `probe()` is read at every show. */
export declare function wireStatusTip(statusHost: HTMLElement, probe: () => unknown): {
  show: () => void; hide: () => void;
  /** Re-render in place while it is up. */
  refresh: () => void;
};

/** The provider status dot on the "…" trigger, and the table it shows on hover. */
export declare function createChatStatusTip(deps: {
  app: DrawingApp;
  statusDot: HTMLElement;
  statusHost: HTMLElement;
}): ChatStatusTip;
