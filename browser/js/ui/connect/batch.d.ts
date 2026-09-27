import type { ConnectListView } from './list.js';

/** Wire the batch bar's Reconnect and Disconnect over the list's selection. */
export declare function wireConnectBatch(
  app: { confirm: (message: string, opts?: object) => Promise<boolean> },
  els: {
    list: HTMLElement;
    /** Read lazily — the connection manager appears after stencil:ready. */
    mgr: () => { reconnectOne: (url: string) => Promise<unknown>; disconnect: (url: string) => void };
    batchBtns: { reconnect: HTMLElement; disconnect: HTMLElement };
  },
  view: ConnectListView,
): void;
