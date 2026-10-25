// A link that opens the Stencil app on a page's image or video (launch URI `source`, `incognito`).

/** `base` with `?source=` (and `&incognito=true`); null for a non-http(s) source or base. */
export declare const shareLink: (base: string, source: string, opts?: { incognito?: boolean }) => string | null;
