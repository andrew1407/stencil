// Shapes for ui/keys.js — the keys half of the rich tooltip: which strings in a composed
// `title` are keys, and the keycaps they draw. The module is byte-pinned with
// browser-extension/src/lib/tip/keys.js (browser-extension/tests/portParity.test.js).

/** Whether the platform draws modifiers as Apple glyphs; false under Node. */
export declare const isMacPlatform: () => boolean;
/** Whether `s` is a key combo and nothing else. */
export declare const isKeyCombo: (s: unknown) => boolean;
/** Render one already-validated combo as `<kbd>` keycaps joined by "+". */
export declare const keysHtml: (combo: string, mac?: boolean) => string;
/** Escape `text` and put keycaps on the key combos found inside it. */
export declare const highlightKeys: (text: unknown, mac?: boolean) => string;
