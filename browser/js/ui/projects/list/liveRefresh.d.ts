import type { DrawingApp } from '../../../core/drawingApp.js';

/** Out-of-band re-lists: connections, peer news, and the one-time open on a lone tab. */
export declare function wireLiveRefresh(deps: {
  app: DrawingApp;
  store: object;
  mayRefresh: () => boolean;
  render: () => void;
  invalidateRemotes: () => void;
  setPeers: (ids: string[]) => void;
  setIncognitoPeers: (list: object[]) => void;
  open: (from: HTMLElement | null) => void;
}): void;
