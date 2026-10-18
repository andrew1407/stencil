// The picture under the CLI stage's header: the editing picture (lines, tint, crop, as rendered)
// or the original, drawn once per request from the export render — no transition, no dust.
export const createCliPeek = (app, slot) => {
  const doc = slot.ownerDocument || globalThis.document;
  let shown = null;   // 'editing' | 'original' | null
  let held = false;
  const img = doc.createElement('img');
  img.className = 'cli-peek-img';
  img.alt = '';
  const draw = (kind, { magnified = false } = {}) => {
    if (!app.image) { hide(); return; }
    const canvas = app.export.renderExportCanvas(kind === 'original' ? 'original' : 'current');
    img.src = canvas.toDataURL ? canvas.toDataURL('image/png') : '';
    img.classList.toggle('is-magnified', magnified);
    if (!img.parentNode) slot.appendChild(img);
    slot.hidden = false;
    shown = kind;
  };
  const hide = () => { shown = null; slot.hidden = true; img.remove(); };
  return {
    show: draw,
    hide,
    shown: () => shown,
    // One click: the editing picture, or away; a double-click: the original, or back to editing.
    toggle(kind) {
      if (kind === 'original') { draw(shown === 'original' ? 'editing' : 'original'); return; }
      if (shown) hide(); else draw('editing');
    },
    // Alt held: a peek that leaves when the key does, unless the picture was shown by a click;
    // Shift pressed or let go meanwhile magnifies it or not.
    hold(kind, opts) { if (shown && !held) return; held = true; draw(kind, opts); },
    magnify(on) { if (held) img.classList.toggle('is-magnified', !!on); },
    // An edit landed: the picture up redraws as it now is.
    refresh() { if (shown) draw(shown, { magnified: img.classList.contains('is-magnified') }); },
    isHeld: () => held,
    release() { if (held) { held = false; hide(); } },
  };
};
