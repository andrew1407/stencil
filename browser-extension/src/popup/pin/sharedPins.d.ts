// Shapes for popup/sharedPins.js — server-backed pins as rows, their Bearer-authed bytes,
// and the polling that keeps them fresh.
import type { PopupImage } from '../list/model.js';

export declare const sharedToImage: (pin: {
  name: string; serverUrl: string; projectId: string; source?: string; resource?: string; color?: string;
}) => PopupImage;
export declare const sharedThumbUrl: (image: PopupImage) => Promise<string>;
export declare const sharedDataUrl: (image: PopupImage) => Promise<string>;
export declare const resolveSharedThumb: (image: PopupImage, thumb: HTMLImageElement) => Promise<void>;
export declare const loadShared: () => Promise<void>;
export declare const syncServerFilterUI: () => void;
export declare const startSharedPolling: () => void;
export declare const stopSharedPolling: () => void;
