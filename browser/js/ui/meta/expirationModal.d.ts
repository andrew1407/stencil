import type { StencilElement } from '../base.js';

/** A local project's expiration: period, calendar day, keep-forever, auto-refresh. */
export declare class StencilExpirationModal extends StencilElement {
  static inner(): string;
  static template(): string;
  /** `anchors`: `from` the clicked row, `backTo` the "⋯" it hung off (the row may be gone by close). */
  /** `onSaved` gets the project's meta once a new expiration is stored. */
  openFor(id: string | number, anchors?: { from?: Element | null; backTo?: Element | null; onSaved?: (meta: Record<string, unknown>) => void }): void;
  wire(app: object): void;
}
