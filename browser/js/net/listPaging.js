// A project listing's cursor walk: each page's `nextCursor` names the next, and a server that
// repeats one or keeps paging is cut off rather than followed forever.

// A server that keeps handing out cursors is cut off here (the cli's max_pages), never followed forever.
export const MAX_LIST_PAGES = 1000;

// The cursor a page names for the next one, '' on the last; a repeat, or one past MAX_LIST_PAGES, throws.
export const nextPageCursor = (body, seen) => {
  const next = body && typeof body.nextCursor === 'string' ? body.nextCursor : '';
  if (!next) return '';
  if (seen.has(next)) throw new Error('GET /projects: the server handed back the same page cursor twice');
  if (seen.size + 1 >= MAX_LIST_PAGES) throw new Error(`GET /projects: kept paging past ${MAX_LIST_PAGES} pages`);
  seen.add(next);
  return next;
};
