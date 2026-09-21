// Shape of the Local file tab (sources/file.js): the element and what its change reports.
import { StencilElement } from '../../base.js';

/** What `source-change` reports: which tab spoke, and what it now holds. */
export interface SourceChangeDetail {
  kind: 'file' | 'url';
  file?: File | null;
  url?: string;
}

export declare class StencilOiFileSource extends StencilElement {
  /** The panel's markup, inlined by the window's markup(). */
  static inner(): string;
  /** `<stencil-oi-file-source>` around inner(), for the window to compose. */
  static template(): string;
  /** The picked file, or null. */
  readonly file: File | null;
  /** The control the tab hands the caret to. */
  readonly field: HTMLElement | null;
  /** Clears the pick and the name beside it. */
  reset(): void;
}
