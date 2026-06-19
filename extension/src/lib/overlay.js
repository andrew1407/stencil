// ── In-page modal (quick-crop) ──────────────────────────────────────────────
// Frames the quick-crop page in a centered modal over the current page. Injected, so
// self-contained (no imports). The framed page posts {source:'stencil-modal',
// type:'ready'|'close'}; if 'ready' never arrives (CSP/mixed-content blocked the frame),
// drop the modal and open a tab.
export const mountStencilModal = (url, title, readyTimeoutMs) => {
  const ID = 'stencil-ext-modal';
  const existing = document.getElementById(ID);
  if (existing) existing.remove();

  const host = document.createElement('div');
  host.id = ID;
  host.style.cssText = 'position:fixed;inset:0;z-index:2147483647;';
  const root = host.attachShadow ? host.attachShadow({ mode: 'open' }) : host;

  const style = document.createElement('style');
  style.textContent = `
    /* Entrance: backdrop fades, panel pops up. Disabled under reduced-motion
       (block at the bottom) since this is injected and self-contained. */
    @keyframes stencilBackdropIn{from{opacity:0}to{opacity:1}}
    @keyframes stencilPanelIn{from{opacity:0;transform:translate(-50%,-46%) scale(.96)}
      to{opacity:1;transform:translate(-50%,-50%) scale(1)}}
    .backdrop{position:fixed;inset:0;background:rgba(0,0,0,.55);
      animation:stencilBackdropIn .2s ease both;}
    .panel{position:fixed;left:50%;top:50%;transform:translate(-50%,-50%);
      width:min(1040px,94vw);height:min(760px,90vh);display:flex;flex-direction:column;
      background:#21242d;border:1px solid #3d4354;border-radius:12px;overflow:hidden;
      box-shadow:0 20px 60px rgba(0,0,0,.6);
      animation:stencilPanelIn .26s cubic-bezier(.16,1,.3,1) both;}
    .bar{display:flex;align-items:center;gap:8px;padding:8px 12px;background:#2b2f3a;
      border-bottom:1px solid #3d4354;color:#e8eaf0;font:600 13px system-ui,sans-serif;}
    .bar .sp{flex:1}
    .bar button{background:#343948;border:1px solid #3d4354;color:#e8eaf0;border-radius:6px;
      width:30px;height:28px;cursor:pointer;font-size:14px;line-height:1;
      transition:background .15s ease,border-color .15s ease,transform .12s ease;}
    .bar button:hover{background:#7c3aed;border-color:#7c3aed;transform:translateY(-1px);}
    .bar button:active{transform:translateY(1px) scale(.96);}
    @media (prefers-reduced-motion: reduce){
      .backdrop,.panel{animation-duration:.001ms;}
      .bar button{transition-duration:.001ms;}
    }
    .loading{position:absolute;left:0;right:0;bottom:0;top:45px;display:flex;
      align-items:center;justify-content:center;color:#9aa0b0;font:13px system-ui,sans-serif;}
    iframe{flex:1;width:100%;border:0;background:#21242d;position:relative;}
    /* Light system preference: mirror lib/theme.css's light palette so the modal
       chrome matches the (theme.css-driven) page framed inside it. Updates live. */
    @media (prefers-color-scheme: light){
      .panel{background:#f4f5f7;border-color:#d4d8e2;}
      .bar{background:#ffffff;border-bottom-color:#d4d8e2;color:#1d2230;}
      .bar button{background:#eceef3;border-color:#d4d8e2;color:#1d2230;}
      .bar button:hover{background:#7c3aed;border-color:#7c3aed;color:#fff;}
      .loading{color:#6b7180;}
      iframe{background:#f4f5f7;}
    }
  `;

  const wrap = document.createElement('div');
  wrap.innerHTML =
    '<div class="backdrop"></div>' +
    '<div class="panel">' +
      '<div class="bar"><span class="title"></span><span class="sp"></span>' +
        '<button class="tab" title="Open in a full tab instead"><svg viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/><polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/></svg></button>' +
        '<button class="close" title="Close (Esc)"><svg viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg></button></div>' +
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
  const cleanup = () => {
    document.removeEventListener('keydown', onKey, true);
    window.removeEventListener('message', onMsg);
    clearTimeout(timer);
  };
  const close = () => {
    cleanup();
    host.remove();
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
