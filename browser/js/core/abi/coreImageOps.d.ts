// The pixel half of the wasm op map: the RGBA filter table and the contour pass, both
// writing their result back into the caller's buffer in place.
import type { CoreOps } from './stencilCore.js';
import type { CoreMarshal } from './coreMarshal.js';

export type ImageOpName = 'applyFilterRGBA' | 'applyContourRGBA';

export declare const buildImageOps: (core: unknown, m: CoreMarshal) => Pick<CoreOps, ImageOpName>;
