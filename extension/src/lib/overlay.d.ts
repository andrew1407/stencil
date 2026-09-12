import type { ShellTheme } from './shellTheme.js';

/** Mounts the quick-crop modal shell; self-contained (handed to executeScript({ func })). */
export declare function mountStencilModal(url: string, title: string, readyTimeoutMs: number, theme?: ShellTheme): void;
