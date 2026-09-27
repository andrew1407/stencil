import type { ChatDock, DockMode } from '../dock.js';

export interface PanelOpenState {
  /** Open or close the panel, playing its dust flight; a close keeps .chat-open until it ends. */
  setOpen: (on: boolean) => void;
  /** The quiet pulse on the toolbar icon (and its fullscreen clone) while a turn runs. */
  markChatBusy: (on: boolean) => void;
  chatDock: ChatDock;
  setDock: (mode: DockMode) => void;
  adoptLayout: () => void;
}

/** The chat panel's open/closed life: dock, dust flight, compact popover and its dismissals. */
export declare function wireOpenState(deps: {
  host: HTMLElement;
  input: HTMLTextAreaElement;
  openBtn: HTMLElement | null;
  resizer: HTMLElement;
  header: HTMLElement;
  backdrop: HTMLElement | null;
  closeBtn: HTMLElement;
  panelIsOpen: () => boolean;
  refreshStatus: () => unknown;
  invalidatePillRects: () => void;
}): PanelOpenState;
