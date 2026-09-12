// Classic script (see accent.js): no ES exports — publishes window.StencilTheme / window.StencilMotion.

export interface StencilTheme {
  modes: string[];
  storageKey: string;
  get(): string;
  resolved(): string;
  set(mode: string, from?: string | Element): string;
  onChange(fn: (mode: string) => void): void;
}

export interface StencilMotion {
  modes: string[];
  labels: Array<[string, string]>;
  storageKey: string;
  get(): string;
  set(mode: string): string;
  reduced(): boolean;
  particles(): boolean;
  style(): string | null;
  styleFrame(style: number, p: unknown, away: boolean, w: number, len: number, tMs: number): unknown;
  edgeJitter(style: number, k: number, points?: number): number;
  edgePolygon(x: number, y: number, w: number, h: number, grow: number, style?: number): string;
  grainShape(shape: string, x: number, y: number, r: number, a: number): unknown;
  shapePolygon(shape: string, x: number, y: number, r: number, a: number): unknown;
  tintOf(style: number): unknown;
  stopOfTint(tint: unknown, k: number): unknown;
  paletteCss(): string[];
  onChange(fn: (mode: string) => void): void;
}
