// ── In-page modal (quick-crop) ──────────────────────────────────────────────
// Frames the quick-crop page in a centered modal over the current page. Injected,
// so self-contained (no imports). The framed page posts {source:'stencil-modal',
// type:'ready'|'close'}; if 'ready' never arrives (CSP / mixed-content blocked the
// frame), drop the modal and open a tab instead.
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
    .backdrop{position:fixed;inset:0;background:rgba(0,0,0,.55);}
    .panel{position:fixed;left:50%;top:50%;transform:translate(-50%,-50%);
      width:min(1040px,94vw);height:min(760px,90vh);display:flex;flex-direction:column;
      background:#21242d;border:1px solid #3d4354;border-radius:12px;overflow:hidden;
      box-shadow:0 20px 60px rgba(0,0,0,.6);}
    .bar{display:flex;align-items:center;gap:8px;padding:8px 12px;background:#2b2f3a;
      border-bottom:1px solid #3d4354;color:#e8eaf0;font:600 13px system-ui,sans-serif;}
    .bar .sp{flex:1}
    .bar button{background:#343948;border:1px solid #3d4354;color:#e8eaf0;border-radius:6px;
      width:30px;height:28px;cursor:pointer;font-size:14px;line-height:1;}
    .bar button:hover{background:#7c3aed;border-color:#7c3aed;}
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
        '<button class="tab" title="Open in a full tab instead">↗</button>' +
        '<button class="close" title="Close (Esc)">✕</button></div>' +
      '<div class="loading">Loading…</div>' +
      '<iframe allow="clipboard-read; clipboard-write"></iframe>' +
    '</div>';
  root.append(style, wrap);
  wrap.querySelector('.title').textContent = title || 'Stencil';
  const frame = wrap.querySelector('iframe');

  const openTab = () => {
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
    if (!d || d.source !== 'stencil-modal') return;
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
