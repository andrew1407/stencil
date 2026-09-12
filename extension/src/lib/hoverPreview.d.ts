export interface Rect { left: number; right: number; top: number; bottom: number; }
export interface Size { width: number; height: number; }
export interface Viewport { width: number; height: number; }

export declare function previewPosition(args: { anchor: Rect; size: Size; viewport: Viewport }): { left: number; top: number };
export declare function previewWorthwhile(image: { w: number; h: number }, thumbPx: number): boolean;

export interface HoverPreviewOptions {
  previewEl: HTMLElement;
  previewImg: HTMLImageElement;
  thumbPx: number;
  fetchDataUrl: (src: string, pageUrl: string) => Promise<string>;
  getSrc: (image: unknown) => string;
  getPageUrl?: (image: unknown) => string;
  win?: Window;
  debounceMs?: number;
}

export interface HoverPreview {
  bind(el: Element, image: unknown): void;
  bindDataUrl(el: Element, small: string, bigger?: () => Promise<string>): void;
  hide(): void;
  cache: Map<string, string>;
}

export declare function createHoverPreview(opts: HoverPreviewOptions): HoverPreview;
