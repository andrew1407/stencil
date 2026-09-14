// Shapes for popup/pinActions.js — the pin toggle, its storage write, and the panel's
// drag-a-URL-in-to-pin surface.
import type { PopupImage } from './model.js';

export declare const download: (src: string) => Promise<number>;
/** Persists the pin, re-sorts the list, and flashes the row. */
export declare const setPinnedState: (image: PopupImage, pinned: boolean) => Promise<void>;
export declare const togglePin: (image: PopupImage) => Promise<void>;
export declare const flashRow: (image: PopupImage, opts?: { landing?: boolean }) => void;
