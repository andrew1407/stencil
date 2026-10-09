// A project listing's cursor walk: each page's `nextCursor` names the next, and a server that
// repeats one or keeps paging is cut off rather than followed forever.

/** Pages a project listing follows before it gives up rather than loop on a server's cursors (the cli's max_pages). */
export declare const MAX_LIST_PAGES: number;
/** The cursor a page names for the next one, '' on the last; a repeat, or one past MAX_LIST_PAGES, throws. */
export declare const nextPageCursor: (body: { nextCursor?: unknown } | null | undefined, seen: Set<string>) => string;
