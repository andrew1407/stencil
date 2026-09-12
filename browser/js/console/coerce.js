// Shared coercions for the console facade and its wrappers.
export const str = (v) => (v == null ? '' : String(v));
