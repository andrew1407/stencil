// The line-style ranges from config/constants.json LIMITS, as the clamps the editor applies and
// the min/max attributes its number fields carry, so every control shares the one range.

/** Into LIMITS.thickMin…thickMax (px). */
export declare const clampThickness: (n: number) => number;
/** Into LIMITS.pointMin…pointMax (px). */
export declare const clampPointSize: (n: number) => number;
/** A stored or scripted thickness / point size: negatives become 0, nothing else moves. */
export declare const storedSize: (n: number) => number;
/** `min="…" max="…"` for a thickness field's markup. */
export declare const THICKNESS_RANGE: string;
/** `min="…" max="…"` for a point-size field's markup. */
export declare const POINT_SIZE_RANGE: string;
