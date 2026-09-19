import type { DrawingApp } from '../../core/drawingApp.js';

/** The two buttons under the list: a fresh editor tab, and Clear All (local projects only). */
export declare function wireListActions(deps: {
  app: DrawingApp;
  els: { newEditorBtn: HTMLElement; clearAllBtn: HTMLElement };
  list: HTMLElement;
  hasServers: () => boolean;
  selected: Map<string, unknown>;
  selectables: Map<string, unknown>;
  doomed: Set<string>;
  updateBatchBar: () => void;
  beginRemoval: () => () => Promise<void>;
  close: () => void;
}): void;
