// Shapes for popup/scan.js — the page/editor scan and the pin/opened/open-in annotation
// passes that run after it (and on their own storage.onChanged echoes).
export declare const scan: () => Promise<void>;
export declare const annotateOpened: () => Promise<void>;
export declare const annotatePinned: () => Promise<void>;
export declare const loadOpenInSettings: () => Promise<void>;
export declare const syncHighlightCheckbox: (tabId: number) => Promise<void>;
