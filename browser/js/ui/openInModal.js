import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { notify } from '../utils.js';
import { icon } from './icons.js';
import { OPEN_IN_DEFAULTS, loadOpenInConfig } from '../config/openInConfig.js';
import {
  buildStencilSchemeUrl, encodeTelegramStartPayload, buildTelegramLink,
} from '../core/deepLink.js';

// Inline hand-offs ride the OS launch machinery (LaunchServices / xdg-open argv), which
// tolerates far less than an in-page URL. Warn on large embedded images; refuse absurd ones.
const INLINE_WARN_CHARS = 200_000;
const INLINE_MAX_CHARS = 1_000_000;

// ── Component: open-in-another-app modal ────────────────────────
// Opened from the toolbar's #open-in-btn (next to Share). Mirrors the CURRENT session
// into another Stencil front-end:
//   • Desktop app — a `stencil://` link the OS routes to the installed desktop app. A
//     server-linked session sends only the server reference (the desktop connects like a
//     fresh client — no token in the link); a local/incognito session embeds the image +
//     full layout inline.
//   • Telegram bot — a t.me deep link carrying (server, project id) in the 64-char
//     `?start=` payload; server projects only (an unsaved session has no id to share, and
//     image bytes can't ride a Telegram link). Overflowing payloads (very long hostnames)
//     fall back to copyable /connect + /fetch commands.
// "Incognito" means Stencil's own never-persisted mode on the receiving side.
export class StencilOpenInModal extends StencilElement {
  static inner() {
    // The Telegram button is always in the markup but hidden until wire() confirms a
    // bot username is configured (config loads async — see loadOpenInConfig).
    const telegram = `<button id="open-in-telegram" class="btn-icon-text" style="display:none;">${icon('message', { size: 14 })}<span>Telegram bot</span></button>`;
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('external', { size: 18 })} Open In…</h2>
                <button class="app-modal-close btn-icon-text" id="open-in-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <div class="vs-section">Open the current project in another app</div>
                <div class="vs-row oi-row" id="open-in-status-row">
                    <label>Project</label>
                    <span class="oi-slot"></span>
                    <span class="footer-hint" id="open-in-status"></span>
                </div>
                <div class="vs-row oi-row">
                    <label>Incognito</label>
                    <input type="checkbox" id="open-in-incognito" class="oi-slot">
                    <span class="footer-hint">Open it there without saving (Stencil incognito mode).</span>
                </div>
                <!-- Fallback shown when a Telegram start payload can't fit in 64 chars. -->
                <div class="vs-row" id="open-in-fallback-row" style="display:none">
                    <label>In the bot</label>
                    <span class="oi-fallback">
                        <code id="open-in-fallback-cmds" style="user-select:all;white-space:pre-line"></code>
                        <button id="open-in-fallback-copy" class="btn-icon" title="Copy commands">${icon('copy', { size: 14 })}</button>
                    </span>
                </div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint" id="open-in-hint"></span>
                <button id="open-in-cancel" class="btn-icon-text">${icon('x', { size: 14 })}<span>Cancel</span></button>
                <button id="open-in-desktop" class="btn-icon-text">${icon('monitor', { size: 14 })}<span>Desktop app</span></button>
                ${telegram}
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-open-in-modal', 'id="open-in-modal-overlay" class="app-modal-overlay"', StencilOpenInModal.inner()); }

  wire(app) {
    const overlay = document.getElementById('open-in-modal-overlay');
    const closeBtn = document.getElementById('open-in-close');
    const cancelBtn = document.getElementById('open-in-cancel');
    const incog = document.getElementById('open-in-incognito');
    const desktopBtn = document.getElementById('open-in-desktop');
    const telegramBtn = document.getElementById('open-in-telegram');
    const statusEl = document.getElementById('open-in-status');
    const hintEl = document.getElementById('open-in-hint');
    const fallbackRow = document.getElementById('open-in-fallback-row');
    const fallbackCmds = document.getElementById('open-in-fallback-cmds');
    const fallbackCopy = document.getElementById('open-in-fallback-copy');

    // Operator config (desktop scheme + optional Telegram bot username) loads from the
    // local openInConfig.json (shared with DrawingApp's toolbar-button gating).
    let cfg = { ...OPEN_IN_DEFAULTS };
    loadOpenInConfig().then(loaded => { cfg = loaded; });

    const { open, close } = wireModalShell(overlay, document.getElementById('open-in-btn'), closeBtn, {
      onOpen: () => {
        incog.checked = app.storage.incognito;   // an incognito session mirrors as incognito
        fallbackRow.style.display = 'none';
        hintEl.textContent = '';
        const remote = app.remoteLink;
        statusEl.textContent = remote
          ? `Server project on ${remote.address}`
          : (app.storage.incognito ? 'Incognito session (image + layout sent inline)'
            : 'Local project (image + layout sent inline)');
        // Unusable targets are HIDDEN, not greyed: Desktop needs a configured scheme;
        // Telegram needs a bot username AND a server project (a 64-char start payload
        // can't carry image bytes, so local/incognito sessions can't ride it). The
        // toolbar's #open-in-btn is itself hidden when neither is available, so at
        // least one of these is always shown by the time the modal opens.
        desktopBtn.style.display = cfg.desktopScheme ? '' : 'none';
        telegramBtn.style.display = (cfg.telegramBotUsername && remote) ? '' : 'none';
      },
    });
    cancelBtn.addEventListener('click', close);

    desktopBtn.addEventListener('click', () => {
      const payload = app.openInLaunchPayload({ incognito: incog.checked });
      const url = payload.server
        ? buildStencilSchemeUrl({
          scheme: cfg.desktopScheme,
          server: payload.server.url,
          id: payload.server.id,
          version: payload.server.version,
          incognito: payload.incognito,
        })
        : buildStencilSchemeUrl({
          scheme: cfg.desktopScheme,
          src: payload.dataUrl,
          layout: payload.layout,
          incognito: payload.incognito,
        });
      if (!payload.server && url.length > INLINE_MAX_CHARS) {
        notify('Image too large to hand off inline — save it to a server and share the server project instead', 'fail');
        return;
      }
      if (!payload.server && url.length > INLINE_WARN_CHARS) {
        notify('Large image — the hand-off may fail; prefer saving to a server', 'info');
      }
      // Hand the custom-scheme URL to the OS. The anchor MUST be in the document —
      // Chrome ignores navigation clicks on a detached anchor, so a hidden data-URL
      // link would silently do nothing. Appended, clicked, then removed; the browser
      // shows its own "Open Stencil?" prompt (no blank tab, unlike window.open).
      const a = document.createElement('a');
      a.href = url;
      a.style.display = 'none';
      document.body.appendChild(a);
      a.click();
      a.remove();
      close();
    });

    telegramBtn?.addEventListener('click', () => {
      const remote = app.remoteLink;
      if (!remote) return;
      const payload = encodeTelegramStartPayload(remote.address, remote.remoteId);
      if (payload) {
        window.open(buildTelegramLink(cfg.telegramBotUsername, payload), '_blank', 'noopener');
        close();
        return;
      }
      // Payload can't fit Telegram's 64-char start limit (very long host) — show the
      // manual recipe instead: open the bot chat and paste the two commands.
      fallbackRow.style.display = '';
      fallbackCmds.textContent = `/connect ${remote.address}\n/fetch ${remote.remoteId}`;
      hintEl.textContent = 'The link is too long for Telegram — open the bot and paste these commands.';
      window.open(`https://t.me/${cfg.telegramBotUsername}`, '_blank', 'noopener');
    });

    fallbackCopy?.addEventListener('click', async () => {
      try {
        await navigator.clipboard.writeText(fallbackCmds.textContent);
        notify('Commands copied', 'ok');
      } catch {
        notify('Could not copy — select the text manually', 'fail');
      }
    });

    return { open, close };
  }
}
define('stencil-open-in-modal', StencilOpenInModal);
