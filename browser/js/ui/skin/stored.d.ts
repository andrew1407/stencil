// The skin kept across launches (extension twin: lib/prefs/shellPrefs.js stencil_skin).
/** The localStorage key holding the skin on at the last close. */
export declare const SKIN_STORAGE_KEY: string;
/** The stored skin ('webcore' | 'aqua'), or null. */
export declare const storedSkin: () => string | null;
/** Stores `name`; null (or an unknown name) forgets it. */
export declare const storeSkin: (name: string | null) => void;
