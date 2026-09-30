// The line-style ranges from config/constants.json LIMITS, as the clamps the editor applies and
// the min/max attributes its number fields carry, so every control shares the one range.
import constants from '../../../../common/config/constants.json' with { type: 'json' };

const { thickMin, thickMax, pointMin, pointMax } = constants.LIMITS;

export const clampThickness = (n) => Math.max(thickMin, Math.min(thickMax, n));
export const clampPointSize = (n) => Math.max(pointMin, Math.min(pointMax, n));

export const THICKNESS_RANGE = `min="${thickMin}" max="${thickMax}"`;
export const POINT_SIZE_RANGE = `min="${pointMin}" max="${pointMax}"`;
