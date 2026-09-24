// The webcore skin's table and the pure rules over it (config/webcore.json).
export interface WebcoreTokens { light: Record<string, string>; dark: Record<string, string>; }
export interface WebcoreCloud { x: number; y: number; blocks: number[][]; shade?: number[][]; }
export interface WebcoreImage {
  width: number; height: number; cell: number; horizonShare: number; skyBands: string[];
  hill: { ampCells: number; spanCells: number; offsetCells: number };
  grass: string[]; cloudInk: string; cloudShade: string; clouds: WebcoreCloud[];
}
export interface WebcoreWord {
  text: string; stroke: string; thickness: number; colors: string[]; grid: [number, number];
  gapCells: number; widthShare: number; centerYShare: number; glyphs: Record<string, number[][]>;
}
export interface WebcoreConfig {
  tokens: WebcoreTokens;
  font: { families: string[]; px: number; pt: number };
  image: WebcoreImage;
  word: WebcoreWord;
  strings: { off: string; project: string; imageName: string };
}
export declare const WEBCORE: WebcoreConfig;
/** The <html> attribute the skin stamps, and its value. */
export declare const SKIN_ATTR: string;
export declare const SKIN_NAME: string;
export declare const OFF_TOAST: string;
export declare const PROJECT_NAME: string;
export declare const IMAGE_NAME: string;

export interface CellGrid { cell: number; cols: number; rows: number; horizon: number; }
export declare const cellGrid: () => CellGrid;
/** The hill crest's row at column cx. */
export declare const hillTopCell: (cx: number, grid: CellGrid) => number;
export declare const skyBandAt: (cy: number, grid: CellGrid) => string;
export declare const grassShadeAt: (cx: number, cy: number, grid: CellGrid) => string;
export declare const cellColorAt: (cx: number, cy: number, grid: CellGrid) => string;

export interface CellRun { x: number; y: number; w: number; h: number; color: string; }
/** Every cloud block in cells, ink before shade. */
export declare const cloudBlocks: () => CellRun[];
/** Same-colour runs along every row of the base picture. */
export declare const cellRuns: (grid: CellGrid) => CellRun[];
/** The finished picture's colour at a pixel. */
export declare const pixelColorAt: (px: number, py: number) => string;

export interface WordPoint { x: number; y: number; }
export interface WordLine {
  points: WordPoint[]; locked: true; color: string; fillColor: string; thickness: number;
}
/** STENCIL as seven closed, locked, filled lines across the sky of a w×h image. */
export declare const wordLines: (w: number, h: number) => WordLine[];
