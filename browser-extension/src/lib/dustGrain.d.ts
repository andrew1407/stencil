// A grain's own shape and the jittered edge a styled cloud wears.
// Byte-pinned twin: browser-extension/src/lib/dustGrain.js (browser-extension/tests/portParity.test.js).
export declare const SHAPE_DISC: 0;
export declare const SHAPE_OVAL: 1;
export declare const SHAPE_WAVE: 2;
export declare const SHAPE_TRIANGLE: 3;
export declare const SHAPE_STREAK: 4;
export declare const STYLED_CELL_SCALE: number;
export declare const WATER_WAVE_SHARE: number;
export declare const FIRE_STREAK_SHARE: number;
export declare const SHAPES: Record<string, Record<string, number>>;
export declare const grainShape: (style: ParticleStyle, w: number) => GrainShape;
export declare const headingOf: (dx: number, dy: number, fromFar: boolean) => number;
export declare const shapePolygon: (shape: GrainShape, x: number, y: number, r: number, a: number,
export declare const addGrainPath: (ctx: CanvasRenderingContext2D, shape: GrainShape, x: number, y: number,
export declare const EDGE_POINTS: number;
export declare const EDGE: Record<'water' | 'fire', Record<string, number>>;
export declare const edgeJitter: (style: ParticleStyle, k: number, points?: number) => number;
export declare const edgeDipOf: (style: ParticleStyle) => number;
export declare const edgeReachOf: (style: ParticleStyle) => number;
export declare const edgeBaseOf: (style: ParticleStyle) => number;
export declare const paletteCss: (stops?: number) => string[];
