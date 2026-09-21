// Shapes for popup/preview.js — the panel-wide instance of lib/hoverPreview.js's magnifier.
export interface HoverPreview {
  cache: Map<string, string>;
  bind(el: Element, image: unknown): void;
  bindDataUrl(el: Element, small: string, bigger?: () => Promise<string>): void;
  hide(): void;
}

export declare const preview: HoverPreview;
/** Shared with the row thumbnails' recovery path and the shared rows' hand-off. */
export declare const previewCache: Map<string, string>;
export declare const bindPreview: HoverPreview['bind'];
export declare const bindDataUrlPreview: HoverPreview['bindDataUrl'];
export declare const hidePreview: HoverPreview['hide'];
