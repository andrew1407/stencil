// The S icon's clock turn during the accent wipe.
export type Span = [number, number];
export declare const ICON_COLS: number;
export declare const cellAngle: (row: number, col: number, cy: number, cx: number) => number;
export declare const sweptSpansAt: (row: number, cols: number, cy: number, cx: number, deg: number) => Span[];
export declare const iconDegrees: (x: number, reach: number) => number;
export declare const iconSpans: (row: number, headerRows: number, cols: number, deg: number, x: number) => Span[];
