/** One saved layout, as Storage restores it (the shape core/projectMeta.js builds). */
export type SavedLayout = Record<string, unknown>;

/** Page size + the custom-size group it reveals. Runs BEFORE applyUnitToUI. */
export function paintPageSize(pageSize: string): void;

/** Line/point styling, the visibility checkboxes and the image filter, off the restored app. */
export function paintDrawingControls(app: Record<string, unknown>, layout: SavedLayout): void;

/** The two visibility checkboxes alone, for a peer layout with nothing else to paint. */
export function paintVisibilityChecks(app: { showPoints: boolean; showLines: boolean }): void;

/** Both copies of each formula field: the top bar's and the context menu's. */
export function paintFormulaFields(app: { formulaX: string; formulaY: string }): void;

/** Hide the docked AND fullscreen selection panels. */
export function hideSelectionPanels(): void;

/** Put the canvas viewport back to its top-left corner. */
export function resetViewportScroll(): void;

/** Scroll the canvas viewport. Synchronous on purpose — the assignment forces the reflow
 *  that any arrival motion must be raised over. */
export function scrollViewportTo(left: number, top: number): void;
