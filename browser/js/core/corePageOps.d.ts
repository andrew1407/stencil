// The page-metric and point-geometry half of the wasm op map: page dimensions, the format
// table, the pixel→page mapping, and the in-place rotate / flip / bounding-box centre.
import type { CoreOps } from './stencilCore.js';
import type { CoreMarshal } from './coreMarshal.js';

export type PageOpName =
  'pageDimensions' | 'pageFormats' | 'pixelToPageRaw' |
  'rotatePoints' | 'flipPoints' | 'boundingBoxCenter';

export declare const buildPageOps: (core: unknown, m: CoreMarshal) => Pick<CoreOps, PageOpName>;
