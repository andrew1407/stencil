import { StencilElement, hostTag, define } from './base.js';
import { notify, detectDesktopOS } from '../utils.js';
import INSTALL from '../config/installConfig.json' with { type: 'json' };
// ── Component: install/download affordance (floating, bottom-right) ──
// A small icon button that, on hover/focus, reveals a menu with two ways to
// get Stencil:
//   • Install web app (PWA)  — only shown once the browser fires
//     `beforeinstallprompt`; clicking replays that deferred event to show the
//     native install dialog. Hidden once installed / already standalone.
//   • Download desktop app   — a direct release-zip link picked for the OS the
//     page is open on (macOS / Windows / Linux), falling back to the releases
//     page when the platform can't be told. Always available.
// Download targets live in config/installConfig.json so they're easy to find
// and bump for new releases. Owns its own markup + behavior, so PWA / download
// concerns never touch DrawingApp.
export class StencilInstall extends StencilElement {
  #deferred = null;

  // Desktop download entry ({label, url}) for the current OS, or the fallback.
  static #desktop() {
    const d = INSTALL.desktop;
    const os = detectDesktopOS();
    return (os && d[os]) || d.fallback;
  }

  static inner() {
    const { label, url } = StencilInstall.#desktop();
    return `<div id="install-menu" role="menu" aria-label="Get Stencil">
        <button type="button" id="install-pwa-btn" role="menuitem" hidden>
          <span class="install-ic">⬇</span><span>Install web app (PWA)</span>
        </button>
        <a id="install-desktop-btn" role="menuitem" href="${url}" download rel="noopener"
           title="Download the Stencil desktop app for ${label}">
          <span class="install-ic">🖥</span><span>Download desktop app</span>
        </a>
      </div>
      <button type="button" id="install-toggle" aria-haspopup="true"
              title="Get Stencil — install or download">⬇</button>`;
  }
  static template() { return hostTag('stencil-install', 'id="install-host"', StencilInstall.inner()); }

  wire(_app) {
    const pwaBtn = this.querySelector('#install-pwa-btn');

    // Already installed / launched as an app → desktop download still applies,
    // but never offer the PWA install option.
    const standalone = matchMedia('(display-mode: standalone)').matches || navigator.standalone === true;
    if (standalone) return;

    // The browser tells us the app is installable: stash the event and reveal
    // the PWA option inside the menu.
    window.addEventListener('beforeinstallprompt', e => {
      e.preventDefault();          // suppress the default mini-infobar; we drive it
      this.#deferred = e;
      pwaBtn.hidden = false;
    });

    // Installed (via our button or the browser's own UI) → tidy up.
    window.addEventListener('appinstalled', () => {
      this.#deferred = null;
      pwaBtn.hidden = true;
      notify('Stencil installed', 'ok');
    });

    pwaBtn.addEventListener('click', async () => {
      if (!this.#deferred) return;
      this.#deferred.prompt();
      const { outcome } = await this.#deferred.userChoice;
      // A prompt can only be used once; drop it and hide regardless of choice.
      this.#deferred = null;
      pwaBtn.hidden = true;
      if (outcome !== 'accepted') notify('Install dismissed', 'info');
    });
  }
}
define('stencil-install', StencilInstall);
