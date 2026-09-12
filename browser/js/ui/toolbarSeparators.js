// The hairlines between toolbar sections (.ctrl-sep) live in a wrapping flex row, so a
// narrowing window can land one at the START of a row — a stray line shoving that section
// right. A separator whose two neighbours sit on different rows is hidden.
export const WRAPPED_SEP_CLASS = 'ctrl-sep-wrapped';
// Hiding one frees its width, which can pull the next section back up — so one pass
// leaves answers the new layout no longer matches. Re-ask until the set stops moving; a
// width that oscillates stops at the cap, hidden (a missing hairline beats a stray one).
const SEP_SETTLE_PASSES = 4;
export function syncWrappedSeparators(root, passes = SEP_SETTLE_PASSES) {
  const seps = [...(root?.querySelectorAll?.('.ctrl-sep') || [])];
  // Every separator shown first, so a given width always resolves the same way and the
  // observer that re-runs this never chases its own change.
  for (const sep of seps) sep.classList.remove(WRAPPED_SEP_CLASS);
  const top = (el) => Math.round(el.getBoundingClientRect().top);
  const straddles = (sep) => {
    const prev = sep.previousElementSibling;
    const next = sep.nextElementSibling;
    return !!prev && !!next && top(next) > top(prev);
  };
  for (let pass = 0; pass < passes; pass++) {
    let moved = false;
    for (const sep of seps) {
      const want = straddles(sep);
      if (want === sep.classList.contains(WRAPPED_SEP_CLASS)) continue;
      sep.classList.toggle(WRAPPED_SEP_CLASS, want);
      moved = true;
    }
    if (!moved) return;   // settled: every hairline agrees with the row it is in
  }
  for (const sep of seps) if (straddles(sep)) sep.classList.add(WRAPPED_SEP_CLASS);   // never settled
}
