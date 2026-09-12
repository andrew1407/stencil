// Pure point-list geometry: bbox centre, rotate, mirror, close-shape test. Each is the
// shared C++ core (wasm) op with the JS body as reference + fallback — a wasm-parity
// surface, kept op-for-op identical.

export interface Point { x: number; y: number; }

export declare const bboxCenterOf: (pts: readonly Point[]) => Point;
/** Rotates in place about (cx, cy) by `angle` radians. */
export declare const rotatePointsAbout: (pts: Point[], cx: number, cy: number, angle: number) => void;
/** Mirrors in place: horizontal flips x' = 2cx - x, vertical flips y' = 2cy - y. */
export declare const flipPointsAbout: (pts: Point[], horizontal: boolean, cx: number, cy: number) => void;
/** Would a point at (x, y) land on the stroke's first point (needs 3+ points)? */
export declare const shouldCloseShape: (points: readonly Point[], x: number, y: number, pointSize: number) => boolean;
