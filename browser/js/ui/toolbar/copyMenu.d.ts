// The Image section's "Make a copy" button: a list of the three scopes, each opening the copy
// confirmation grown out of its row.
import type { DrawingApp } from '../../core/drawingApp.js';

export declare function wireCopyProjectMenu(trigger: HTMLElement | null, app: DrawingApp): void;

/** The toolbar's copy button, where a copy started from the canvas menu flies home (null unwired). */
export declare const copyProjectButton: () => HTMLElement | null;
