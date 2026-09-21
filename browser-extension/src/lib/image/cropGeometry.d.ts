// PORT of browser/js/core/parse/cropGeometry.js (browser-extension/tests/portParity.test.js), plus
// the extension's own page-size table and dialog helpers.
export interface CropRect { x: number; y: number; width: number; height: number; }
export interface PageSize { width: number; height: number; }

export declare const PAGE_SIZES: Record<string, PageSize>;
export declare const DEFAULT_PAGE: string;
export declare const pageSizeLabel: (name: string) => string;
export declare const pageSizeOptions: () => string;

export declare const isAlbumOrientation: (width: number, height: number) => boolean;
export declare const cropAspect: (pageWidth: number, pageHeight: number, album: boolean) => number;
export declare const centeredCrop: (imageW: number, imageH: number, aspectWoverH: number) => CropRect;
export declare const resizeCropFromCorner: (cur: CropRect, corner: 0 | 1 | 2 | 3, cursorX: number, cursorY: number,
  aspectWoverH: number, imageW: number, imageH: number, minSize?: number) => CropRect;
export declare const moveCropClamped: (cur: CropRect, dx: number, dy: number, imageW: number, imageH: number) => CropRect;
export declare const scaleCropCentered: (cur: CropRect, factor: number, aspectWoverH: number,
  imageW: number, imageH: number, minSize?: number) => CropRect;
export declare const roundRect: (r: CropRect, iw: number, ih: number) => CropRect;
export declare const pageDims: (page: string, customW?: number, customH?: number) => PageSize;
