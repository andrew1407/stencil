// Shapes for options/pins.js — the pinned-images viewer, grouped by pinning site and
// cross-referenced against every connected server's stored pins.

/** Re-reads which connected servers store each pinned source URL (the gold outline
 * and the "stored on" filter); safe to call with no connections. */
export declare const refreshServerPins: () => Promise<void>;
/** Re-renders the whole list against the current site/store/search filters. */
export declare const renderPins: () => Promise<void>;
