// Which theme a single shot is taken in. A name ending in -light/-dark says so itself;
// otherwise the app's config decides, through the mode table below. 'random' is seeded, so
// a re-run reproduces the same picture.
const SUFFIXED = /-(light|dark)$/;

// FNV-1a over the step name, mixed with the config's seed: stable across runs and machines.
const hashStep = (seed, step) => {
  let hash = (seed >>> 0) ^ 0x811c9dc5;
  for (const ch of step) {
    hash = (hash ^ ch.charCodeAt(0)) >>> 0;
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash;
};

export const THEME_RESOLVERS = Object.freeze({
  light: () => 'light',
  dark: () => 'dark',
  system: (_step, theme) => theme.systemPrefers,
  random: (step, theme) => (hashStep(theme.randomSeed, step) % 2 ? 'dark' : 'light'),
});

export const resolveThemeMode = (mode, step, theme) =>
  (THEME_RESOLVERS[mode] || THEME_RESOLVERS.dark)(step, theme);

// A picker for one app: `pick('crop-modal')` → 'light' | 'dark'.
export const makeThemePicker = (theme) => (step) => {
  const suffix = SUFFIXED.exec(step);
  if (suffix) return suffix[1];
  return resolveThemeMode(theme.steps[step] || theme.mode, step, theme);
};

// The two halves of a Light|Dark pair, in the order they are documented.
export const pairNames = (base) => [`${base}-light`, `${base}-dark`];
