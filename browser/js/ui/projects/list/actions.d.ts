import type { DrawingApp } from '../../../core/drawingApp.js';

/** What Clear All Local plays out: the local saved rows and their `local:` keys; server rows stay. */
export declare const localLeavers: (list: ParentNode, keys: Iterable<string>) => { rows: Element[]; keys: string[] };
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
