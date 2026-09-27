// Shapes for popup/pin/sharedPins.js — server-backed pins as rows, their Bearer-authed bytes,
// and the polling that keeps them fresh.
import type { PopupImage } from '../list/model.js';

export declare const sharedToImage: (pin: {
  name: string; serverUrl: string; projectId: string; source?: string; resource?: string; color?: string;
}) => PopupImage;
export declare const sharedThumbUrl: (image: PopupImage) => Promise<string>;
export declare const sharedDataUrl: (image: PopupImage) => Promise<string>;
export declare const resolveSharedThumb: (image: PopupImage, thumb: HTMLImageElement) => Promise<void>;
/** Resolves true when any server's list changed since the last load. */
export declare const loadShared: () => Promise<boolean>;
export declare const syncServerFilterUI: () => void;
export declare const startSharedPolling: () => void;
export declare const stopSharedPolling: () => void;
