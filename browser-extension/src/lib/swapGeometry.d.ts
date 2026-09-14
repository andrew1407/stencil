// Classic script (see accent.js): no ES exports — extends window.StencilKit with the
// palette-swap geometry (wipe edge, dust motes, easing).

export interface SwapGeometryKit {
  DUST_LIFE_MS: number;
  DUST_MAX_T: number;
  DUST_MIN_T: number;
  DUST_MOTES: number;
  EDGE_POINTS: number;
  STYLE_FIRE: number;
  STYLE_WATER: number;
  SWAP_MS: number;
  bezierY(t: number, x1: number, y1: number, x2: number, y2: number): number;
  dustEase(t: number): number;
  dustNoise(a: number, b: number): number;
  edgeDipOf(style: number): number;
  edgeJitter(style: number, k: number, points: number): number;
  edgePolygon(x: number, y: number, w: number, h: number, grow: number, style?: number): string;
  fract(v: number): number;
  originOf(ref: string | Element | null | undefined): { x: number; y: number } | null;
  styleCode(): number;
}
