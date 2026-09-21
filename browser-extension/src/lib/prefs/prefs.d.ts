// Classic script (see accent.js): no ES exports — publishes window.StencilKit.

export interface StencilAccentDef { key: string; label: string; hex: string; }

export interface StencilKit {
  ACCENTS: StencilAccentDef[];
  DEFAULT: string;
  KEY: string;
  MKEY: string;
  MOTION_DEFAULT: string;
  MOTION_LABELS: Array<[string, string]>;
  MOTION_MODES: string[];
  applyFavicon(key: string): void;
  applyMotion(mode: string): void;
  faviconSvg(hex: string): string;
  has(key: string): boolean;
  hexOf(key: string): string;
  isMotion(mode: string): boolean;
  mirror(obj: Record<string, unknown>): void;
  motionReduced(): boolean;
  needsDarkGlyph(hex: string): boolean;
  onAccentInk(hex: string): string;
  particleStyle(): string | null;
  read(): string;
  readMotion(): string;
  readPref<T>(key: string, valid: (v: string | null) => boolean, fallback: T): string | T;
  writePref(key: string, value: string): void;
}
