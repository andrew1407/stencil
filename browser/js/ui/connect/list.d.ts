import type { ConnectRowDrag } from './rowDrag.js';

/** The removal hold (motion.js createListHold): a wipe in flight defers the next render. */
export interface ConnectListHold {
  readonly holding: boolean;
  begin: () => () => Promise<void>;
  finalizeAll: () => void;
}

/** The list state the batch bar, the connect form and the modal shell act on. */
export interface ConnectListView {
  selected: Set<string>;
  /** Rows playing their removal dust: out of the selection pool until the settle render. */
  doomed: Set<string>;
  hold: ConnectListHold;
  drag: ConnectRowDrag;
  render: () => void;
  updateBatchBar: () => void;
  /** Pin the list's height and start a hold; await the returned settle after the wipe. */
  beginRemoval: () => () => Promise<void>;
}

/** Wire the Servers list over its elements; `mgr` is read lazily (it appears after stencil:ready). */
export declare function createConnectList(app: { confirm: (message: string, opts?: object) => Promise<boolean> }, els: {
  overlay: HTMLElement;
  list: HTMLElement;
  reconnectBtn: HTMLButtonElement;
  filterEl: HTMLSelectElement;
  batchBar: HTMLElement;
  batchCount: HTMLElement;
  selectAllBtn: HTMLElement | null;
  selectedGroup: HTMLElement;
  mgr: () => object | null | undefined;
}): ConnectListView;
