/** The live handle ui/shell.js registers for one wired window. */
export interface ModalShellApi {
  open(from?: unknown, backTo?: unknown, opts?: { stacked?: boolean }): void;
  /** `backTo` overrides where this close lands; anything that is not an anchor is ignored. */
  close(backTo?: unknown): void;
  openPopover(anchorEl: unknown): void;
  toggle(from?: unknown): void;
  isOpen(): boolean;
  readonly stacked: boolean;
  takesEscape(): boolean;
}

/** Every wired shell, in wire order. */
export declare const modalShells: Set<ModalShellApi>;

/** Installs the single document-level Escape listener; safe to call per shell. */
export declare function wireEscapeOnce(): void;

/** Close every open window except `except`; returns the first one closed, else null. */
export declare function closeOpenModal(except?: ModalShellApi | null): ModalShellApi | null;
