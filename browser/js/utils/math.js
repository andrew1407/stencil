// The model stores lengths in centimetres; `unit` ('cm' | 'mm' | 'in') only affects display.
export const CM_PER_INCH = 2.54;
export const MM_PER_CM = 10;
export const cmToUnit = (cm, unit) => {
  if (unit === 'in') return cm / CM_PER_INCH;
  return unit === 'mm' ? cm * MM_PER_CM : cm;
};
export const unitToCm = (val, unit) => {
  if (unit === 'in') return val * CM_PER_INCH;
  return unit === 'mm' ? val / MM_PER_CM : val;
};
export const unitLabel = (unit) => (unit === 'in' || unit === 'mm' ? unit : 'cm');

export const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
