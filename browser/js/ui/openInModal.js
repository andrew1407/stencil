import { StencilElement, hostTag, define, wireModalShell } from './base.js';
import { notify } from '../utils.js';
import { icon } from './icons.js';
import { OPEN_IN_DEFAULTS, loadOpenInConfig } from '../config/openInConfig.js';
import {
  buildStencilSchemeUrl, encodeTelegramStartPayload, buildTelegramLink,
} from '../core/launch/deepLink.js';

// Inline hand-offs ride the OS launch machinery (LaunchServices / xdg-open argv), which
// tolerates far less than an in-page URL.
const INLINE_WARN_CHARS = 200_000;
const INLINE_MAX_CHARS = 1_000_000;

// A server-linked session sends only the server reference — no token; local and incognito embed
// image + layout inline. The bot's 64-char `?start=` payload is server projects only.
export class StencilOpenInModal extends StencilElement {
  #openFor;
  static inner() {
    // Hidden until wire() confirms a bot username is configured (config loads async).
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
                        <button id="open-in-fallback-copy" class="btn-icon" data-title="Copy commands">${icon('copy', { size: 14 })}</button>
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

  // A project other than the one being edited (the projects list's row menu); `anchors` is
  // the flight's two ends (ui/base.js open(from, backTo)).
  openFor(id, anchors) { this.#openFor?.(id, anchors); }

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

    // Shared with DrawingApp's toolbar-button gating.
    let cfg = { ...OPEN_IN_DEFAULTS };
    loadOpenInConfig().then(loaded => { cfg = loaded; });

    // null = the live session, an id = a row of the projects list. Cleared on close.
    let targetId = null;
    const targetRemote = () => {
      if (targetId == null) return app.remoteLink
        ? { address: app.remoteLink.address, remoteId: app.remoteLink.remoteId } : null;
      const meta = app.storage.store.getMeta(targetId);
      return (meta?.remoteId && meta?.address)
        ? { address: meta.address, remoteId: meta.remoteId } : null;
    };

    const { open, close } = wireModalShell(overlay, document.getElementById('open-in-btn'), closeBtn, {
      onOpen: () => {
        // A row hand-off is never the session's incognito state.
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
        // Unusable targets are hidden, not greyed: Telegram needs a bot username AND a server
        // project (64 chars can't carry image bytes). #open-in-btn hides when neither is available.
        desktopBtn.style.display = cfg.desktopScheme ? '' : 'none';
        telegramBtn.style.display = (cfg.telegramBotUsername && remote) ? '' : 'none';
      },
      onClose: () => { targetId = null; },
    });
    cancelBtn.addEventListener('click', close);
    // Stacked: opens over the projects list; the toolbar button's open() still replaces.
    this.#openFor = (id, { from = null, backTo = null } = {}) => {
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
      // The anchor MUST be in the document — Chrome ignores navigation clicks on a detached one.
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
      // The payload can't fit Telegram's 64-char start limit: show the manual recipe.
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
