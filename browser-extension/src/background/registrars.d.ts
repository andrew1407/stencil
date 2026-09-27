// Shapes for background/registrars.js — injection into already-open tabs, plus the
// content-script registration pass (editor bridge, page API, editor page API).

/** The right-click probe's files, in run order (manifest.json lists the same). */
export declare const CTX_PROBE_FILES: string[];
/** window.stencil's four MAIN-world parts, in run order. */
export declare const PAGE_API_MAIN_FILES: string[];
/** Inject the right-click probe into every open http(s) tab. */
export declare const injectProbeIntoOpenTabs: () => Promise<void>;
/** (Un)register the named script sets against the current settings, and inject the
 * ones that just gained a match into already-open tabs. */
export declare const setUpScripts: (opts?: { keys?: string[]; inject?: boolean | string[] }) => Promise<void>;
