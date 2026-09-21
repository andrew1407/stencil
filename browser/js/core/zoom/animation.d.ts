// The two zooms that move the scroll offset with the scale, split out of ZoomPan: an
// eased 200 ms zoom about the viewport centre, and a focal zoom that pins an image-space
// point under its on-screen position. Both read app state and the clamp through `zp`.
import type { ZoomPan } from './pan.js';

export declare const zoomAroundCenter: (zp: ZoomPan, newScale: number) => void;
export declare const zoomToImagePoint: (zp: ZoomPan, newScale: number, imgX: number, imgY: number) => void;
