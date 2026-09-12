// The model stores lengths in centimetres; `unit` ('cm' | 'in') only affects display.
export const CM_PER_INCH = 2.54;
export const cmToUnit = (cm, unit) => (unit === 'in' ? cm / CM_PER_INCH : cm);
export const unitToCm = (val, unit) => (unit === 'in' ? val * CM_PER_INCH : val);
export const unitLabel = (unit) => (unit === 'in' ? 'in' : 'cm');

export const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
