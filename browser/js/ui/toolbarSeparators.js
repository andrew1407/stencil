// The hairline before a toolbar section (.ctrl-section::before) is painted in the column
// gap, out of flow, so a narrowing window can land one at the START of a row — a stray
// line beside nothing. A section that begins a row drops it.
export const WRAPPED_SEP_CLASS = 'ctrl-sep-wrapped';
// One pass is the whole answer: the hairline costs no width, so marking it cannot move the
// wrap that decided it. Measure every top first, then mark — no read between two writes.
export function syncWrappedSeparators(root) {
  const sections = [...(root?.querySelectorAll?.('.ctrl-section') || [])];
  const tops = sections.map((el) => Math.round(el.getBoundingClientRect().top));
  sections.forEach((sec, i) => sec.classList.toggle(WRAPPED_SEP_CLASS, i === 0 || tops[i] > tops[i - 1]));
}
