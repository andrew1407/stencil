// Shapes for popup/pinDialog.js — "where do you want to pin this?", then the local pin
// and (if a server was chosen) the project save.
import type { PopupImage } from './model.js';

export declare const pinWithPrompt: (image: PopupImage, anchor?: Element | null) => Promise<void>;
