// Injected confirm for a drop on an OCCUPIED editor: resolves 'replace' | 'newtab' |
// 'cancel'. executeScript serialises the function and nothing else, so it stays
// self-contained (no imports); the SW awaits the returned promise.
// `mode` is the Appearance choice resolved by the panel (lib/shellTheme.js injectedScheme);
// it falls back to the page's query only when nothing has been mirrored yet. A bare
// `@media (prefers-color-scheme)` here would ignore the choice outright.
export const mountDropChoice = (accent = '#7c3aed', mode = 'system') => new Promise((resolve) => {
  const ID = 'stencil-ext-dropchoice';
  document.getElementById(ID)?.remove();
  const host = document.createElement('div');
  host.id = ID;
  host.style.cssText = 'position:fixed;inset:0;z-index:2147483647;display:flex;align-items:center;justify-content:center;background:rgba(0,0,0,.38);';
  const root = host.attachShadow ? host.attachShadow({ mode: 'open' }) : host;
  let prefersDark = false;
  try { prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches; } catch (e) { /* no matchMedia */ }
  const dark = mode === 'dark' || (mode !== 'light' && prefersDark);
  // Card colours from lib/theme/palette.css, like lib/dropZones.js.
  const cardBg = dark ? 'rgba(33,36,45,.97)' : 'rgba(244,245,247,.98)';
  const cardFg = dark ? '#e8eaf0' : '#1d2230';
  const style = document.createElement('style');
  style.textContent = `
    .card{background:${cardBg};color:${cardFg};border:1px solid ${accent};border-radius:12px;
      padding:18px 20px;max-width:340px;font:14px/1.45 system-ui,sans-serif;box-shadow:0 12px 40px rgba(0,0,0,.5);}
    .card p{margin:0 0 12px;font-weight:600;}
    .btns{display:flex;flex-direction:column;gap:8px;}
    button{font:600 13px system-ui,sans-serif;padding:8px 12px;border-radius:8px;cursor:pointer;
      border:1px solid ${accent};background:transparent;color:${cardFg};}
    button:hover{background:color-mix(in srgb, ${accent} 25%, transparent);}
    button.primary{background:${accent};color:#fff;}
  `;
  const card = document.createElement('div');
  card.className = 'card';
  const msg = document.createElement('p');
  msg.textContent = 'This editor already holds an image. Where should the dropped image open?';
  const done = (v) => { window.removeEventListener('keydown', onKey, true); host.remove(); resolve(v); };
  const onKey = (e) => { if (e.key === 'Escape') { e.stopPropagation(); done('cancel'); } };
  const mk = (label, val, primary) => {
    const b = document.createElement('button');
    b.textContent = label;
    if (primary) b.className = 'primary';
    b.addEventListener('click', () => done(val));
    return b;
  };
  const btns = document.createElement('div');
  btns.className = 'btns';
  btns.append(mk('Open here (replace the image)', 'replace', true), mk('Open in a new tab', 'newtab'), mk('Cancel', 'cancel'));
  card.append(msg, btns);
  root.append(style, card);
  host.addEventListener('mousedown', (e) => { if (e.target === host) done('cancel'); });
  window.addEventListener('keydown', onKey, true);
  (document.body || document.documentElement).appendChild(host);
});
