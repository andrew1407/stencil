import type { StencilElement } from '../base.js';

/** An element, a client-rect-shaped box, or null: where a flight starts or lands. */
export type ConfirmAnchor = Element | { left: number; top: number; width: number; height: number } | null;

export interface ConfirmOptions {
  title?: string; titleIcon?: string; confirmLabel?: string; cancelLabel?: string;
  confirmIcon?: string; danger?: boolean;
  /** Overrides the gesture the dialog would otherwise grow out of. */
  openAnchor?: ConfirmAnchor;
  /** Where the close lands; a function is called with the raw answer. */
  closeAnchor?: ConfirmAnchor | ((answer: unknown) => ConfirmAnchor);
}
export interface ConfirmAltOptions extends ConfirmOptions { altLabel?: string; altIcon?: string; }
export interface ChooseOptions extends ConfirmOptions { options?: { value: string; label?: string }[]; }
export interface PromptOptions extends ConfirmOptions {
  defaultValue?: string; multiline?: boolean; rows?: number;
  validate?: (trimmed: string) => string;
}

/** The reusable yes/no modal behind app.confirm(); resolves false on any dismissal. */
export declare class StencilConfirmModal extends StencilElement {
  static inner(): string;
  static template(): string;
  wire(): void;
  ask(message: string, opts?: ConfirmOptions): Promise<boolean>;
  /** Cancel | altLabel | confirmLabel, resolving 'confirm', 'alt' or null. */
  askAlt(message: string, opts?: ConfirmAltOptions): Promise<'confirm' | 'alt' | null>;
  choose(message: string, opts?: ChooseOptions): Promise<string | null>;
  prompt(message: string, opts?: PromptOptions): Promise<string | null>;
}
