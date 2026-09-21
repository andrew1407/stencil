// Shapes for popup/openActions.js — every way a row hands its image off: into an editor
// tab, a new editor tab, crop, or an external app.
import type { PopupImage } from './list/model.js';

/** SVGs rasterise first, so the hand-off always carries pixels. */
export declare const imageDataUrl: (image: PopupImage) => Promise<string>;
export declare const openInDesktop: (image: PopupImage) => Promise<void>;
export declare const openInTelegram: (image: PopupImage) => void;
export declare const sendToEditor: (image: PopupImage, incognito: boolean, open?: string) => Promise<void>;
/** Falls back to a new-tab hand-off when no open editor tab already holds the image. */
export declare const resumeInEditor: (image: PopupImage) => Promise<void>;
export declare const openHere: (
  image: PopupImage, incognito: boolean, open: string | undefined, anchor?: Element | null,
) => Promise<boolean | void>;
export declare const openCrop: (image: PopupImage) => Promise<void>;
