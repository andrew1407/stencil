// The on-page 2×2 drop overlay, injected while a row is dragged out of the panel via
// executeScript({func}) — so mountDropZones must stay SELF-CONTAINED, no imports. A drop
// only completes from the SIDE PANEL; DROPZONES_DISARM tears it down on drag end.
// Inlined again inside mountDropZones (an injected func can't call module scope).
export const quadrantAt = (x, y, w, h) => {
  const left = x < w / 2, top = y < h / 2;
  return top ? (left ? 'here' : 'incognito') : (left ? 'newtab' : 'crop');
};

// `mode` is the Appearance choice UNRESOLVED: only the target page can answer what the OS
// prefers. Never a bare `@media (prefers-color-scheme)` — that follows the PAGE's scheme.
export const mountDropZones = (accent = '#7c3aed', editor = false, mode = 'system') => {
  if (window.__stencilDropZones) return;
  window.__stencilDropZones = true;

  const ID = 'stencil-ext-dropzones';
  document.getElementById(ID)?.remove();

  // `editor` = this page IS a live editor, so the labels say so.
  const QUADS = [
    { key: 'here', label: editor ? 'Open in this editor' : 'Open in editor here', row: 1, col: 1 },
    { key: 'incognito', label: editor ? 'Open incognito here' : 'Open incognito', row: 1, col: 2 },
    { key: 'newtab', label: 'Open image in new tab', row: 2, col: 1 },
    { key: 'crop', label: editor ? 'Crop in this editor' : 'Crop', row: 2, col: 2 },
  ];

  const host = document.createElement('div');
  host.id = ID;
  host.style.cssText = 'position:fixed;inset:0;z-index:2147483647;pointer-events:none;opacity:0;transition:opacity .12s ease;';
  const root = host.attachShadow ? host.attachShadow({ mode: 'open' }) : host;

  let prefersDark = false;
  try { prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches; } catch (e) { /* no matchMedia */ }
  const dark = mode === 'dark' || (mode !== 'light' && prefersDark);
  // Panel colours from lib/theme/palette.css, at .69/.72 alpha so the page still reads.
  const cellBg = dark ? 'rgba(33,36,45,.69)' : 'rgba(244,245,247,.72)';
  const cellFg = dark ? '#e8eaf0' : '#1d2230';
  const scrim = dark ? 'rgba(0,0,0,.28)' : 'rgba(255,255,255,.3)';
  const overMix = dark ? 30 : 22;

  const style = document.createElement('style');
  style.textContent = `
    .grid{position:fixed;inset:0;display:grid;grid-template-columns:1fr 1fr;grid-template-rows:1fr 1fr;
      gap:14px;padding:14px;box-sizing:border-box;background:${scrim};backdrop-filter:blur(2px);}
    .cell{display:flex;align-items:center;justify-content:center;flex-direction:column;gap:12px;
      border:5px dashed ${accent};border-radius:16px;
      background:${cellBg};color:${cellFg};
      font:700 19px system-ui,sans-serif;text-align:center;padding:16px;
      box-shadow:0 8px 32px rgba(0,0,0,.45);
      transition:background .12s ease,border-color .12s ease;}
    /* The icon pulses (small → large → small) to catch the eye, like the editor's drop zones. */
    .cell .ic{color:${accent};transform-origin:center;animation:stencilDropPulse .9s ease-in-out infinite;}
    .cell .label{max-width:90%;}
    .cell.over{background:color-mix(in srgb, ${accent} ${overMix}%, ${cellBg});border-style:solid;}
    @keyframes stencilDropPulse{0%,100%{transform:scale(.8)}50%{transform:scale(1.25)}}
    @media (prefers-reduced-motion: reduce){ .cell .ic{animation-duration:.001ms;} .cell{transition-duration:.001ms;} }
  `;

  const grid = document.createElement('div');
  grid.className = 'grid';
  // Inlined from lib/icons.js (an injected func can't import): monitor / incognito / external / crop.
  const PATHS = {
    here: '<rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/>',
    incognito: '<path d="M2 12h20"/><path d="M5 12l1.6-5.3A2 2 0 0 1 8.5 5.3h7a2 2 0 0 1 1.9 1.4L19 12"/><circle cx="6.5" cy="15.5" r="2.8"/><circle cx="17.5" cy="15.5" r="2.8"/><path d="M9.3 15a2.8 2.8 0 0 1 5.4 0"/>',
    newtab: '<path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/><polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/>',
    crop: '<path d="M6.13 1L6 16a2 2 0 0 0 2 2h15"/><path d="M1 6.13L16 6a2 2 0 0 1 2 2v15"/>',
  };
  const svgIcon = (inner) => `<svg class="ic" viewBox="0 0 24 24" width="52" height="52" fill="none" `
    + `stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${inner}</svg>`;
  const cells = {};
  for (const q of QUADS) {
    const c = document.createElement('div');
    c.className = 'cell';
    c.style.gridArea = `${q.row} / ${q.col}`;
    c.innerHTML = `${svgIcon(PATHS[q.key])}<span class="label">${q.label}</span>`;
    cells[q.key] = c;
    grid.appendChild(c);
  }
  root.append(style, grid);

  const quadAt = (x, y) => {
    const left = x < window.innerWidth / 2;
    const top = y < window.innerHeight / 2;
    return top ? (left ? 'here' : 'incognito') : (left ? 'newtab' : 'crop');
  };

  // Mirrors lib/dragUrl.js extractDraggedUrl; custom MIME types don't survive a
  // cross-document drag, so only the standard text types are read.
  const readUrl = (dt) => {
    const get = (t) => { try { return dt.getData(t) || ''; } catch { return ''; } };
    const html = get('text/html');
    const m = html && html.match(/<(?:img|source|video)[^>]+src\s*=\s*["']([^"']+)["']/i);
    if (m) return m[1];
    const uriList = get('text/uri-list');
    if (uriList) {
      const line = uriList.split('\n').map((s) => s.trim()).find((s) => s && !s.startsWith('#'));
      if (line) return line;
    }
    const text = get('text/plain').trim();
    if (/^https?:\/\//i.test(text) || text.startsWith('data:')) return text;
    return '';
  };

  const show = () => {
    host.style.pointerEvents = 'auto';
    host.style.opacity = '1';
  };
  const highlight = (key) => {
    for (const k in cells) cells[k].classList.toggle('over', k === key);
  };

  let removed = false;
  const remove = () => {
    if (removed) return;
    removed = true;
    window.removeEventListener('dragenter', onOver, true);
    window.removeEventListener('dragover', onOver, true);
    window.removeEventListener('drop', onDrop, true);
    window.removeEventListener('dragleave', onLeave, true);
    window.removeEventListener('keydown', onKey, true);
    clearTimeout(timer);
    host.remove();
    window.__stencilDropZones = false;
  };

  const carriesUrl = (dt) => {
    const t = dt && dt.types;
    return !!(t && (t.includes('text/uri-list') || t.includes('text/html') || t.includes('text/plain')));
  };

  // The zones OWN the drag: capture-phase stopPropagation keeps the page's own drag UI
  // out, or the editor page's native overlay handles the drop twice.
  const onOver = (e) => {
    if (!carriesUrl(e.dataTransfer)) return;
    e.preventDefault();
    e.stopPropagation();
    try { e.dataTransfer.dropEffect = 'copy'; } catch { /* noop */ }
    show();
    highlight(quadAt(e.clientX, e.clientY));
  };
  const onDrop = (e) => {
    if (!carriesUrl(e.dataTransfer)) { remove(); return; }
    e.preventDefault();
    e.stopPropagation();
    const url = readUrl(e.dataTransfer);
    const action = quadAt(e.clientX, e.clientY);
    remove();
    if (url) {
      // Literal MSG.PAGE_DROP (lib/messages.js): an injected func can't import — keep in sync.
      try { chrome.runtime.sendMessage({ type: 'stencil-page-drop', action, url }); } catch { /* SW asleep */ }
    }
  };
  // Leaving the window only clears the highlight; the overlay stays up for a re-entry.
  const onLeave = (e) => { if (!e.relatedTarget) highlight(null); };
  const onKey = (e) => { if (e.key === 'Escape') remove(); };

  window.addEventListener('dragenter', onOver, true);
  window.addEventListener('dragover', onOver, true);
  window.addEventListener('drop', onDrop, true);
  window.addEventListener('dragleave', onLeave, true);
  window.addEventListener('keydown', onKey, true);
  // A popup drag whose window closed before its dragend could DISARM would strand the overlay.
  const timer = setTimeout(remove, 12_000);

  (document.body || document.documentElement).appendChild(host);
  show();
};

// Injected on DROPZONES_DISARM; self-contained for the same reason as mountDropZones.
export const unmountDropZones = () => {
  document.getElementById('stencil-ext-dropzones')?.remove();
  window.__stencilDropZones = false;
};
