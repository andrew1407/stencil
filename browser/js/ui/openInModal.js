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
// Mirrors the CURRENT session into another Stencil front-end: the Desktop app via a
// `stencil://` link (a server-linked session sends only the server reference — no
// token; local/incognito embeds image + layout inline), or the Telegram bot via a
// t.me link carrying (server, project id) in the 64-char `?start=` payload — server
// projects only; overflow falls back to copyable /connect + /fetch commands.
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

  // Hand off a project OTHER than the one being edited — the projects list's per-row
  // "Open in another app". `anchors` is the flight's two ends, as everywhere a window is
  // raised from a row menu (ui/base.js open(from, backTo)).
  openFor(id, anchors) { this._openFor?.(id, anchors); }

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

    // The project being handed off: null = the live session (the toolbar button), an id =
    // that row of the projects list. Cleared on close so the toolbar button never inherits
    // the last row's target.
    let targetId = null;
    // Its server linkage, which decides the status line and whether Telegram can be offered.
    const targetRemote = () => {
      if (targetId == null) return app.remoteLink
        ? { address: app.remoteLink.address, remoteId: app.remoteLink.remoteId } : null;
      const meta = app.storage.store.getMeta(targetId);
      return (meta?.remoteId && meta?.address)
        ? { address: meta.address, remoteId: meta.remoteId } : null;
    };

    const { open, close } = wireModalShell(overlay, document.getElementById('open-in-btn'), closeBtn, {
      onOpen: () => {
        // A row hand-off is never the session's incognito state — that belongs to what is
        // open here, not to the saved project being sent.
        incog.checked = targetId == null && app.storage.incognito;
        fallbackRow.style.display = 'none';
        hintEl.textContent = '';
        const remote = targetRemote();
        const name = targetId == null ? null : (app.storage.store.getMeta(targetId)?.name || 'Untitled');
        statusEl.textContent = remote
          ? `Server project on ${remote.address}`
          : (targetId != null ? `"${name}" (image + layout sent inline)`
            : (app.storage.incognito ? 'Incognito session (image + layout sent inline)'
              : 'Local project (image + layout sent inline)'));
        // Unusable targets are HIDDEN, not greyed: Desktop needs a configured scheme;
        // Telegram needs a bot username AND a server project (64 chars can't carry
        // image bytes). The toolbar's #open-in-btn hides when neither is available.
        desktopBtn.style.display = cfg.desktopScheme ? '' : 'none';
        telegramBtn.style.display = (cfg.telegramBotUsername && remote) ? '' : 'none';
      },
      onClose: () => { targetId = null; },
    });
    cancelBtn.addEventListener('click', close);
    // Stacked: raised from a row of the projects list, so it opens OVER it. The toolbar
    // button's own open() still replaces whatever is showing.
    this._openFor = (id, { from = null, backTo = null } = {}) => {
      targetId = id;
      open(from, backTo, { stacked: true });
    };

    desktopBtn.addEventListener('click', () => {
      const payload = app.openInLaunchPayload({ incognito: incog.checked, id: targetId });
      if (!payload) { notify('That project could not be read from storage', 'fail'); return; }
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
      // Chrome ignores navigation clicks on a detached anchor. Appended, clicked,
      // removed; the browser shows its own "Open Stencil?" prompt (no blank tab).
      const a = document.createElement('a');
      a.href = url;
      a.style.display = 'none';
      document.body.appendChild(a);
      a.click();
      a.remove();
      close();
    });

    telegramBtn?.addEventListener('click', () => {
      const remote = targetRemote();
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
