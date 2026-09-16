// Shared coercions for the console facade and its wrappers.
export const str = (v) => (v == null ? '' : String(v));

// One keyword per entry: an array is taken as given (a keyword may be several words), a
// string splits on COMMAS and newlines only — a space is part of the keyword, not a
// separator (ui/keywordChips.js normalizeKeyword). Blanks drop; the store de-duplicates.
export const splitKeywords = (v) => (Array.isArray(v) ? v : [v])
  .flatMap((k) => (Array.isArray(k) ? k : str(k).split(/[,\n]+/)))
  .map((k) => str(k).trim().replace(/\s+/g, ' '))
  .filter(Boolean);
