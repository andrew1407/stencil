// ── Length units ────────────────────────────────────────────────
// The model always stores lengths in centimetres; `unit` ('cm' | 'in') only
// controls how they are shown/entered. 1 inch = 2.54 cm.
export const CM_PER_INCH = 2.54;
export const cmToUnit = (cm, unit) => (unit === 'in' ? cm / CM_PER_INCH : cm);
export const unitToCm = (val, unit) => (unit === 'in' ? val * CM_PER_INCH : val);
export const unitLabel = (unit) => (unit === 'in' ? 'in' : 'cm');

// THE shared bound: the app had five hand-rolled copies of Math.min(hi, Math.max(lo, v)).
export const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
