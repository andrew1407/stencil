// The line-style ranges from config/constants.json LIMITS, as the clamps the editor applies and
// the min/max attributes its number fields carry, so every control shares the one range.
import constants from '../../../../common/config/constants.json' with { type: 'json' };

const { thickMin, thickMax, pointMin, pointMax } = constants.LIMITS;

export const clampThickness = (n) => Math.max(thickMin, Math.min(thickMax, n));
export const clampPointSize = (n) => Math.max(pointMin, Math.min(pointMax, n));

// A size a line stores or a script sets (px): never negative, 0 included (no points drawn), no cap,
// so a stored layout comes back as it was written. The control clamps above are the editor's range.
export const storedSize = (n) => Math.max(0, n);

export const THICKNESS_RANGE = `min="${thickMin}" max="${thickMax}"`;
export const POINT_SIZE_RANGE = `min="${pointMin}" max="${pointMax}"`;
