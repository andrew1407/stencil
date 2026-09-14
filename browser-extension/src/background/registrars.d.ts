// Shapes for background/registrars.js — injection into already-open tabs, plus the
// content-script registration pass (editor bridge, page API, editor page API).

/** Inject the right-click probe into every open http(s) tab. */
export declare const injectProbeIntoOpenTabs: () => Promise<void>;
/** (Un)register the named script sets against the current settings, and inject the
 * ones that just gained a match into already-open tabs. */
export declare const setUpScripts: (opts?: { keys?: string[]; inject?: boolean | string[] }) => Promise<void>;
