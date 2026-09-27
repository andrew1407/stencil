import type { ConnectListView } from './list.js';

/** Wire the connect form (Connect, Enter in either field) and Reconnect all. */
export declare function wireConnectForm(
  els: {
    urlEl: HTMLInputElement;
    tokenEl: HTMLInputElement;
    addBtn: HTMLButtonElement;
    reconnectBtn: HTMLButtonElement;
    list: HTMLElement;
    /** Read lazily — the connection manager appears after stencil:ready. */
    mgr: () => {
      connect: (target: string | { url: string; token: string }) => Promise<unknown>;
      isExpired: (url: string) => boolean;
      reconnect: () => Promise<unknown>;
      readonly urls: string[];
    };
  },
  view: Pick<ConnectListView, 'hold' | 'render'>,
): void;
