import type { ApiPart } from './apiPart.js';
export interface WindowSpec {
  key: string;
  title: string;
  overlay: string;
  opener: string | string[];
  hotkey?: string;
  aliases?: string[];
}
export declare const WINDOWS: readonly WindowSpec[];
export declare const createWindowsApi: () => ApiPart;
