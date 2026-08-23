// ── In-page modal (quick-crop) ──────────────────────────────────────────────
// Frames the quick-crop page in a centered modal over the current page. Injected, so
// self-contained (no imports). The framed page posts {source:'stencil-modal',
// type:'ready'|'close'}; if 'ready' never arrives (CSP/mixed-content blocked the frame),
// drop the modal and open a tab.
//
// THEME: the shell can't link lib/theme.css (it lives in someone else's page), so its
// palette arrives as DATA — `theme` = {mode, accent, palettes, accents} from
// lib/shellTheme.js. It used to be hardcoded light-with-a-prefers-color-scheme-override,
// which put a WHITE frame around a dark crop page whenever the user's Appearance choice
// disagreed with the OS. The values become CSS custom properties on the host element,
// so a live theme/accent change (chrome.storage mirror) just re-sets them.
export const mountStencilModal = (url, title, readyTimeoutMs, theme) => {
  const ID = 'stencil-ext-modal';
  const existing = document.getElementById(ID);
  if (existing) existing.remove();

  const host = document.createElement('div');
  host.id = ID;
  host.style.cssText = 'position:fixed;inset:0;z-index:2147483647;';
  const root = host.attachShadow ? host.attachShadow({ mode: 'open' }) : host;

  // ── Palette (data in, CSS variables out) ──
  const t = theme || {};
  const prefersDark = () => {
    try { return !!(window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches); }
    catch (e) { return false; }
  };
  // Mirror of lib/shellTheme.js resolveShellMode — an injected fn can't import.
  const resolveMode = (mode) => (mode === 'dark' || mode === 'light' ? mode : (prefersDark() ? 'dark' : 'light'));
  const FALLBACK = {
    dark: { bg: '#21242d', panel: '#2b2f3a', panel2: '#343948', line: '#3d4354', text: '#e8eaf0', muted: '#9aa0b0' },
    light: { bg: '#f4f5f7', panel: '#ffffff', panel2: '#eceef3', line: '#d4d8e2', text: '#1d2230', muted: '#6b7180' },
  };
  const applyTheme = (mode, accent) => {
    const resolved = resolveMode(mode);
    const p = ((t.palettes || FALLBACK)[resolved]) || FALLBACK[resolved];
    // Readable from outside the shadow root (tests, and anything asking what it drew).
    host.setAttribute('data-stencil-theme', resolved);
    for (const k in p) host.style.setProperty('--st-' + k, p[k]);
    host.style.setProperty('--st-accent', accent || '#7c3aed');
    host.style.colorScheme = resolved;   // native scrollbars/controls inside the shell
  };
  applyTheme(t.mode, t.accent);

  // ── The sand (the extension's own grain, written out inline) ──
  // The #app-tooltip mask from lib/theme.css: three coprime dot screens (4/7/11px, at
  // different phases) dying at different rates so the specks thin out in sequence. At
  // 120% the dots overlap outright, so a settled panel is solid to the pixel. `d` is
  // 0 (settled) … 1 (dispersed).
  const grain = (d) => {
    const stop = (rate) => `radial-gradient(circle at 50% 50%,#000 ${Math.max(0, 120 - rate * d)}%,`
      + `transparent ${Math.max(0, 128 - rate * d)}%)`;
    return `${stop(160)},${stop(140)},${stop(125)}`;
  };
  // Chrome does NOT interpolate gradients inside mask-image (it flips at the midpoint), so
  // the dissolve is written out as a ramp of discrete steps rather than two endpoints —
  // which is what sand does anyway. `ends` carries the opacity/transform for 0% and 100%.
  // The cell sizes ride along in every step: Chrome resolves mask-size against the
  // FIRST layer while mask-image is animating, which would flatten the three coprime
  // grids into one lattice — and one lattice lines its dots up into joining rectangles,
  // which is exactly what stops it reading as sand.
  const SIZES = '4px 4px,7px 7px,11px 11px';
  const grainFrames = (name, levels, ends) => `@keyframes ${name}{` + levels.map((d, i) => {
    const pct = Math.round((i / (levels.length - 1)) * 100);
    const edge = i === 0 ? ends[0] : (i === levels.length - 1 ? ends[1] : '');
    return `${pct}%{${edge}-webkit-mask-image:${grain(d)};mask-image:${grain(d)};`
      + `-webkit-mask-size:${SIZES};mask-size:${SIZES};}`;
  }).join('') + '}';
  const FORM = [1, 0.82, 0.64, 0.48, 0.32, 0.16, 0];

  const style = document.createElement('style');
  style.textContent = `
    /* Isolation: everything is scoped inside the shadow root, and the few INHERITED
       properties that would otherwise cross the boundary (font, colour, spacing,
       direction) are reset here — the host page must not be able to restyle the shell. */
    :host{all:initial;}
    *{box-sizing:border-box;margin:0;padding:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
      letter-spacing:normal;text-transform:none;direction:ltr;}
    /* Entrance/exit: the panel FORMS OUT OF SAND and disperses again — the same grain the
       extension's own surfaces use (lib/animations.css). It is the MASK form of the
       effect, not the cloned-mote form the menus play: this panel frames a live <iframe>,
       and a mote layer would mean cloning that iframe a hundred-odd times. Disabled under
       reduced-motion (block at the bottom). */
    @keyframes stencilBackdropIn{from{opacity:0}to{opacity:1}}
    @keyframes stencilBackdropOut{from{opacity:1}to{opacity:0}}
    ${grainFrames('stencilPanelIn', FORM,
      ['opacity:0;transform:translate(-50%,-46%) scale(.96);',
       'opacity:1;transform:translate(-50%,-50%) scale(1);'])}
    ${grainFrames('stencilPanelOut', [...FORM].reverse(),
      ['opacity:1;transform:translate(-50%,-50%) scale(1);',
       'opacity:0;transform:translate(-50%,-52%) scale(.97);'])}
    .backdrop{position:fixed;inset:0;background:rgba(0,0,0,.55);
      animation:stencilBackdropIn .2s ease both;}
    .wrap.leaving .backdrop{animation:stencilBackdropOut .26s ease both;}
    /* Colours come from the host's CSS variables (set from the user's Appearance +
       accent choice), so the shell matches the framed page and re-themes live. */
    .panel{position:fixed;left:50%;top:50%;transform:translate(-50%,-50%);
      width:min(1040px,94vw);height:min(760px,90vh);display:flex;flex-direction:column;
      background:var(--st-bg);border:1px solid var(--st-line);border-radius:12px;overflow:hidden;
      box-shadow:0 20px 60px rgba(0,0,0,.45);
      -webkit-mask-size:4px 4px,7px 7px,11px 11px;
      mask-size:4px 4px,7px 7px,11px 11px;
      -webkit-mask-position:0 0,2px 3px,5px 1px;
      mask-position:0 0,2px 3px,5px 1px;
      -webkit-mask-composite:source-over;mask-composite:add;
      animation:stencilPanelIn .34s cubic-bezier(.16,1,.3,1) both;}
    .wrap.leaving .panel{animation:stencilPanelOut .26s cubic-bezier(.4,0,1,1) both;}
    .bar{display:flex;align-items:center;gap:8px;padding:8px 12px;background:var(--st-panel);
      border-bottom:1px solid var(--st-line);color:var(--st-text);font:600 13px system-ui,sans-serif;}
    .bar .sp{flex:1}
    /* Ghost buttons matching the extension's .icon-btn: panel-2 fill, hairline border,
       accent on hover — legible on either palette. */
    .bar button{background:var(--st-panel2);border:1px solid var(--st-line);color:var(--st-text);
      border-radius:6px;width:30px;height:28px;cursor:pointer;font-size:14px;line-height:1;
      display:inline-flex;align-items:center;justify-content:center;
      transition:background .15s ease,border-color .15s ease,color .15s ease,transform .12s ease;}
    .bar button:hover{background:var(--st-accent);border-color:var(--st-accent);color:#fff;transform:translateY(-1px);}
    .bar button:focus-visible{outline:2px solid var(--st-accent);outline-offset:2px;}
    .bar button:active{transform:translateY(1px) scale(.96);}
    .bar button svg{display:block;--ic-on:0;overflow:visible;}
    /* Per-icon hover motion for the two glyphs this shell carries, on the canonical
       values (browser js/config/iconMotion.json, ported in lib/animations.css — this
       surface is injected and can't link it): the arrow LEAVES the box, and the cross
       is struck out one stroke at a time, because close/clear/disconnect all mean
       "make this go away". Transform / stroke-dashoffset only, so the bar can't reflow. */
    @keyframes stencilDrawSlash{from{stroke-dashoffset:17}to{stroke-dashoffset:0}}
    .bar button svg,.bar button svg *{transition:transform .2s cubic-bezier(.16,1,.3,1);}
    .bar button svg *{transform-box:view-box;}
    .bar button.tab:hover svg{--ic-on:1;}
    .bar button.tab svg .ic-arrow{transform:translate(calc(var(--ic-on)*1.4px),calc(var(--ic-on)*-1.4px));}
    .bar button.close svg .ic-stroke{stroke-dasharray:17;}
    .bar button.close:hover svg .ic-stroke{animation:stencilDrawSlash .18s cubic-bezier(.33,1,.68,1) both;}
    .bar button.close:hover svg .ic-stroke:nth-of-type(2){animation-delay:.18s;}
    @media (prefers-reduced-motion: reduce){
      .backdrop,.panel{animation-duration:.001ms;}
      /* A half-formed panel is a surprise, not motion — show it whole. */
      .panel{-webkit-mask-image:none !important;mask-image:none !important;}
      .bar button{transition-duration:.001ms;}
      /* The glyph stays in its rest pose — which is also each motion's end state. */
      .bar button svg,.bar button svg *{--ic-on:0 !important;animation:none !important;transition:none !important;}
    }
    .loading{position:absolute;left:0;right:0;bottom:0;top:45px;display:flex;
      align-items:center;justify-content:center;color:var(--st-muted);font:13px system-ui,sans-serif;}
    iframe{flex:1;width:100%;border:0;background:var(--st-bg);position:relative;}
  `;

  const wrap = document.createElement('div');
  wrap.className = 'wrap';   // .wrap.leaving is what plays the dispersal
  // The ONE place the extension still uses a native `title`: this shell is injected into
  // the HOST page (executeScript({func}) — it can't import), so lib/controlTooltip.js
  // never runs over it and data-title alone would leave the two icons unexplained.
  wrap.innerHTML =
    '<div class="backdrop"></div>' +
    '<div class="panel">' +
      '<div class="bar"><span class="title"></span><span class="sp"></span>' +
        '<button class="tab" title="Open in a full tab instead"><svg viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/><g class="ic-arrow"><polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/></g></svg></button>' +
        '<button class="close" title="Close (Esc)"><svg viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><line class="ic-stroke" x1="18" y1="6" x2="6" y2="18"/><line class="ic-stroke" x1="6" y1="6" x2="18" y2="18"/></svg></button></div>' +
      '<div class="loading">Loading…</div>' +
      '<iframe allow="clipboard-read; clipboard-write"></iframe>' +
    '</div>';
  root.append(style, wrap);
  wrap.querySelector('.title').textContent = title || 'Stencil';
  const frame = wrap.querySelector('iframe');

  const openTab = () => {
    // Literal MSG.OPEN_TAB (lib/messages.js): this fn is injected via
    // executeScript({func}) — serialized, so it can't import. Keep in sync by hand.
    try { chrome.runtime.sendMessage({ type: 'stencil-open-tab', url }); }
    catch { window.open(url, '_blank'); }
  };
  // Live re-theme: lib/accent.js mirrors the Appearance mode + accent KEY into
  // chrome.storage.local on every change, so flipping the theme from the popup's
  // moon button re-paints an OPEN modal instead of waiting for the next one.
  // (Literal key strings — an injected fn can't import lib/shellTheme.js.)
  const onStore = (changes, area) => {
    if (area !== 'local') return;
    if (!changes.stencil_theme && !changes.stencil_accent) return;
    const mode = changes.stencil_theme ? changes.stencil_theme.newValue : t.mode;
    const key = changes.stencil_accent ? changes.stencil_accent.newValue : '';
    const accent = (key && (t.accents || {})[key]) || t.accent;
    t.mode = mode;
    t.accent = accent;
    applyTheme(mode, accent);
  };
  try { chrome.storage.onChanged.addListener(onStore); } catch (e) { /* no chrome.storage here */ }

  const cleanup = () => {
    document.removeEventListener('keydown', onKey, true);
    window.removeEventListener('message', onMsg);
    try { chrome.storage.onChanged.removeListener(onStore); } catch (e) { /* noop */ }
    clearTimeout(timer);
  };
  // The modal disperses on the way out too. Listeners go at once (so a second Escape or
  // a click on the dissolving panel can do nothing) and the node is torn down on a timer
  // — idempotent, so every close route lands on "gone" however many of them fire.
  const LEAVE_MS = 260;
  let leaving = false;
  const close = () => {
    if (leaving) return;
    leaving = true;
    cleanup();
    wrap.classList.add('leaving');
    host.style.pointerEvents = 'none';
    setTimeout(() => host.remove(), LEAVE_MS);
  };
  const onKey = (e) => { if (e.key === 'Escape') close(); };
  const onMsg = (e) => {
    const d = e.data;
    if (!d || d.source !== 'stencil-modal') return;   // literal SRC.MODAL (injected fn — can't import)
    if (d.type === 'ready') {
      clearTimeout(timer);
      const l = root.querySelector('.loading');
      if (l) l.remove();
    } else if (d.type === 'close') {
      close();
    }
  };
  const timer = setTimeout(() => {
    close();
    openTab();
  }, readyTimeoutMs || 3000);

  window.addEventListener('message', onMsg);
  document.addEventListener('keydown', onKey, true);
  wrap.querySelector('.close').onclick = close;
  wrap.querySelector('.backdrop').onclick = close;
  wrap.querySelector('.tab').onclick = () => {
    close();
    openTab();
  };

  frame.src = url;
  (document.body || document.documentElement).appendChild(host);
};
