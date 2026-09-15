export type ThemeName = 'light' | 'dark';
export type ThemeMode = ThemeName | 'system' | 'random';

export interface ThemeBlock {
  mode: ThemeMode;
  modes: ThemeMode[];
  systemPrefers: ThemeName;
  randomSeed: number;
  steps: Record<string, ThemeMode>;
}

export interface Budget { pngMaxBytes: number; gifMaxBytes: number }
export interface GifLook { fps: number; width: number; colors: number; scale: string }

export declare class CaptureConfig {
  constructor(app: string, shared: object, own: object);
  static of(app: string): CaptureConfig;
  readonly app: string;
  readonly shared: any;
  readonly own: any;
  readonly budget: Budget;
  readonly gifLook: GifLook;
  readonly vscode: any;
  readonly serverUrl: string;
  readonly theme: ThemeBlock;
  get<T = any>(dotted: string, fallback?: T): T;
  prompt(key: string): string;
  stubPlan(key: string): any;
  url(key: string): string;
  serverToken(): string | null;
}

export function loadCaptureConfig(app: string): CaptureConfig;
