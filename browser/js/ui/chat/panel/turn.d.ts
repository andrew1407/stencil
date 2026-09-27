export interface PanelTurn {
  /** Run one logged turn; rejects with the turn's typed error so stencil.prompt sees it. */
  runTurn: (text: string) => Promise<unknown>;
  readonly isSending: boolean;
  /** Stop the turn in flight; true when one was actually running. */
  abort: () => boolean;
}

/** The chat panel's one turn in flight, and the toast a turn landing on a closed panel leaves. */
export declare function createPanelTurn(deps: {
  transcript: HTMLElement;
  ctrl: () => unknown;
  panelIsOpen: () => boolean;
  setOpen: (on: boolean) => void;
  markChatBusy: (on: boolean) => void;
  refreshStatus: () => unknown;
  renderAttachments: () => void;
  updateControls: () => void;
}): PanelTurn;
