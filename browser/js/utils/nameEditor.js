// ── Inline name editor (shared: topbar + projects list) ─────────
// Wire a live-validated inline rename editor. ✓ enables only when the trimmed value
// changed AND validates (rejection reason shows on ✓'s tooltip); Enter commits, Escape
// cancels. mousedown preventDefault keeps focus so the click fires before any blur
// handler (callers wire blur→cancel). `alwaysShow` keeps ✓/✗ visible (projects list);
// otherwise they appear once the value differs (topbar). Returns { refresh }.
export const wireNameEditor = (input, acceptBtn, cancelBtn, { current, validate, commit, cancel, alwaysShow = false }) => {
  const refresh = () => {
    const v = input.value.trim();
    const changed = v !== (current() || '');
    if (!alwaysShow) {
      acceptBtn.style.display = changed ? '' : 'none';
      cancelBtn.style.display = changed ? '' : 'none';
    }
    if (!changed) { acceptBtn.disabled = true; acceptBtn.dataset.title = 'No change'; return; }
    const res = validate(v) || { ok: true, reason: '' };
    acceptBtn.disabled = !res.ok;
    acceptBtn.dataset.title = res.ok ? 'Save name (Enter)' : res.reason;
  };
  const doCommit = () => {
    const v = input.value.trim();
    if (!acceptBtn.disabled && v !== (current() || '')) commit(v);
  };
  input.addEventListener('input', refresh);
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') { e.preventDefault(); doCommit(); }
    else if (e.key === 'Escape') { e.preventDefault(); cancel(); }
  });
  for (const b of [acceptBtn, cancelBtn]) b.addEventListener('mousedown', (e) => e.preventDefault());
  acceptBtn.addEventListener('click', doCommit);
  cancelBtn.addEventListener('click', () => cancel());
  refresh();
  return { refresh };
};


// ── Display-shortening for project / image names ─────────────────────────────
// For names interpolated into dialog sentences/toasts, where CSS text-overflow can't help.
// Middle ellipsis because both ends carry meaning: the head is what the user recognises,
// the tail holds the extension / "-copy" suffix that says WHICH item this is.
// Ported to extension/src/lib/displayName.js and desktop/src/support/displayName.hpp
// — keep the three behaviourally identical (same limit, same head/tail split).
export const NAME_DISPLAY_CHARS = 28;
export const shortName = (name, limit = NAME_DISPLAY_CHARS) => {
  const s = String(name ?? '');
  if (s.length <= limit) return s;
  // Reserve one char for the ellipsis; give the extra char to the head on odd splits.
  const keep = limit - 1;
  const head = Math.ceil(keep / 2);
  const tail = keep - head;
  return `${s.slice(0, head)}…${tail > 0 ? s.slice(-tail) : ''}`;
};
