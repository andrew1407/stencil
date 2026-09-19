export interface ConnectRowDrag {
  /** Make one row draggable: reorder on an in-list drop, disconnect on a drop outside. */
  attach: (row: HTMLElement, url: string) => void;
  /** True while a drag is live, so a connections event does not re-render under it. */
  isDragging: () => boolean;
}

export declare function createConnectRowDrag(deps: {
  list: HTMLElement;
  overlay: HTMLElement;
  /** Read lazily — the connection manager appears after stencil:ready. */
  mgr: () => { urls: string[]; reorder: (order: readonly string[]) => void };
  render: () => void;
  confirmDisconnect: (url: string) => Promise<void>;
}): ConnectRowDrag;
