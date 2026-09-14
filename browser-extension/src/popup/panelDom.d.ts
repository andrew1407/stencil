// Shapes for popup/panelDom.js — the panel's shared DOM handles and host-mode flags,
// looked up once at module load.
export interface ThemePref {
  modes: string[];
  storageKey: string;
  get(): string;
  resolved(): string;
  set(mode: string, from?: Element | string | null): string;
  onChange(fn: (mode: string) => void): void;
}

export declare const listEl: HTMLElement;
export declare const statusEl: HTMLElement;
export declare const clearStatus: () => void;
export declare const countEl: HTMLElement;
export declare const previewEl: HTMLElement;
export declare const previewImg: HTMLImageElement;
export declare const menuEl: HTMLElement;
export declare const THUMB_PX: number;
export declare const PLAY_THUMB: string;
/** The three host documents (popup / side panel / DevTools panel) are told apart by path. */
export declare const IS_SIDE_PANEL: boolean;
export declare const IS_DEVTOOLS: boolean;
export declare const dismiss: () => void;
export declare const run: (fn: () => Promise<void> | void) => Promise<void>;
/** Stamped on <html> by lib/shellPrefs.js before first paint; undefined outside a page context. */
export declare const themePref: ThemePref | undefined;
export declare const themeBtn: HTMLElement | null;
