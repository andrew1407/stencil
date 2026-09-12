/** One layout as a server (or a co-editing peer) sends it. */
export type ServerLayout = Record<string, unknown> | null;

/** Adopt the layout's filter/tint into the editor and its controls; clears filterDirty. */
export function adoptServerFilter(app: Record<string, unknown>, layout: ServerLayout): void;

/** Adopt the layout's page format (named size or custom cm dims) into state + the page UI. */
export function adoptServerPageFormat(app: Record<string, unknown>, layout: ServerLayout): void;

/** Adopt the layout's x/y formulas; the expressions are kept whether or not they are allowed. */
export function adoptServerFormulas(app: Record<string, unknown>, layout: ServerLayout): void;
