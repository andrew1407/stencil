// The <stencil-confirm-modal> asks that are not a plain confirm. Each falls back to what a
// pre-wire or non-DOM caller can safely assume, never to a blocking native dialog.

// Resolves the chosen option value, or null on cancel. opts.options: [{ value, label }].
export const askChoose = (el, message, opts = {}) => {
  if (el && typeof el.choose === 'function') return el.choose(message, opts);
// No modal (tests / pre-wire): the first option, if any.
  const first = (opts.options || [])[0];
  return Promise.resolve(first ? first.value : null);
};

// Cancel | alt | confirm → 'confirm', 'alt', or null. opts: { title, confirmLabel, altLabel }.
export const askAlt = (el, message, opts = {}) => {
  if (el && typeof el.askAlt === 'function') return el.askAlt(message, opts);
  return Promise.resolve(null);
};

// Resolves the trimmed string, or null on cancel. opts: { title, confirmLabel, defaultValue }.
export const askPrompt = (el, message, opts = {}) => {
  if (el && typeof el.prompt === 'function') return el.prompt(message, opts);
  return Promise.resolve(opts.defaultValue ?? null);
};
