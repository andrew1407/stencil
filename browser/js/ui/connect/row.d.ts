import type { ConnectRowDrag } from './rowDrag.js';

/** The connection manager's slice a row reads: the live record behind a URL. */
export interface ConnectRowSource {
  get: (url: string) => { status?: string; connected?: boolean; credentialKind?: string;
    mintInvite: () => Promise<string> } | undefined;
}

/** Build one Servers row for `url`; the list appends it and owns every piece of state it reads. */
export declare function connectRow(url: string, cm: ConnectRowSource, deps: {
  app: { prompt: (message: string, opts?: object) => Promise<string | null> };
  /** Read lazily — the connection manager appears after stencil:ready. */
  mgr: () => { reconnectOne: (url: string, token?: string) => Promise<unknown> };
  selected: Set<string>;
  drag: ConnectRowDrag;
  updateBatchBar: () => void;
  render: () => void;
  confirmDisconnect: (url: string) => Promise<void>;
}): HTMLElement;
