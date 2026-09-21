// Shape of the URL tab (sources/url.js): the element, its guard and what it announces.
import { StencilElement } from '../../base.js';

/** What `preview-request` reports: the address the tab wants fetched. */
export interface PreviewRequestDetail { url: string; }

/** True for an http(s), data: or blob: address — what Preview is gated on. */
export declare const isPreviewableUrl: (v: string | null | undefined) => boolean;

export declare class StencilOiUrlSource extends StencilElement {
  static inner(): string;
  static template(): string;
  /** The typed address, trimmed. */
  readonly url: string;
  /** The control the tab hands the caret to. */
  readonly field: HTMLInputElement | null;
  /** Empties the field and disables Preview. */
  reset(): void;
}
