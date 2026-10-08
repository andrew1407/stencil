/** The live handle ui/modal/shell.js registers for one wired window. */
export interface ModalShellApi {
  /**
   * `from` is the control (or a client rect) the opening flies out of, `backTo` where the close
   * returns; `at` puts the window's top-left corner on a client point, inside the viewport, and a
   * window already up at full size only moves there.
   */
  open(from?: unknown, backTo?: unknown, opts?: { stacked?: boolean; at?: { x: number; y: number } | null }): void;
  /** `backTo` overrides where this close lands; anything that is not an anchor is ignored. */
  close(backTo?: unknown): void;
  openPopover(anchorEl: unknown): void;
  toggle(from?: unknown): void;
  isOpen(): boolean;
  /** The id of the overlay the shell is wired over (null when it has none). */
  readonly overlayId: string | null;
  readonly stacked: boolean;
  takesEscape(): boolean;
}

/** The shell wired over `overlayId`, else null. */
export declare function shellFor(overlayId: string | null | undefined): ModalShellApi | null;

/** Every wired shell, in wire order. */
export declare const modalShells: Set<ModalShellApi>;

/** True when an open window answered this Escape keydown (so fullscreen keeps it). */
export declare function windowTookEscape(e: unknown): boolean;

/** Installs the single document-level Escape listener; safe to call per shell. */
export declare function wireEscapeOnce(): void;

/** Close every open window except `except`; returns the first one closed, else null. */
export declare function closeOpenModal(except?: ModalShellApi | null): ModalShellApi | null;
