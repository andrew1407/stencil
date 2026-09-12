// ── Injected confirm for a drop on an OCCUPIED editor ───────────────────────
// Its own module because it is injected on its own — executeScript serialises the
// function and nothing else, so it has to stay self-contained (no imports).

// Injected confirm for a drop on an OCCUPIED editor: resolves 'replace' | 'newtab' |
// 'cancel'. executeScript awaits the returned promise, so the SW just reads the answer.
// Self-contained (no imports), like the overlay above.
export const mountDropChoice = (accent = '#7c3aed') => new Promise((resolve) => {
  const ID = 'stencil-ext-dropchoice';
  document.getElementById(ID)?.remove();
  const host = document.createElement('div');
  host.id = ID;
  host.style.cssText = 'position:fixed;inset:0;z-index:2147483647;display:flex;align-items:center;justify-content:center;background:rgba(0,0,0,.38);';
  const root = host.attachShadow ? host.attachShadow({ mode: 'open' }) : host;
  const style = document.createElement('style');
  style.textContent = `
    .card{background:rgba(33,36,45,.97);color:#e8eaf0;border:1px solid ${accent};border-radius:12px;
      padding:18px 20px;max-width:340px;font:14px/1.45 system-ui,sans-serif;box-shadow:0 12px 40px rgba(0,0,0,.5);}
    .card p{margin:0 0 12px;font-weight:600;}
    .btns{display:flex;flex-direction:column;gap:8px;}
    button{font:600 13px system-ui,sans-serif;padding:8px 12px;border-radius:8px;cursor:pointer;
      border:1px solid ${accent};background:transparent;color:#e8eaf0;}
    button:hover{background:color-mix(in srgb, ${accent} 25%, transparent);}
    button.primary{background:${accent};color:#fff;}
    @media (prefers-color-scheme: light){ .card{background:rgba(244,245,247,.98);color:#1d2230;} button{color:#1d2230;} button.primary{color:#fff;} }
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
