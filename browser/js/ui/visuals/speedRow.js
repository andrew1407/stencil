// One speed row of the Visuals window: a number box and a slider showing one value, the
// slider applying live and the box on change, both through the shared motion setter.
export const wireSpeedRow = ({ range, number, key, app }) => {
  // The bar fills to its thumb (the scrub's --scrub-fill).
  const paint = (v) => {
    range.value = v;
    number.value = Number(v).toFixed(2);
    range.style.setProperty('--scrub-fill', `${((v - range.min) / (range.max - range.min)) * 100}%`);
  };
  range.addEventListener('input', () => paint(app.settings.setMotion(key, range.value)[key]));
  number.addEventListener('change', () => paint(app.settings.setMotion(key, number.value)[key]));
  return { paint };
};
