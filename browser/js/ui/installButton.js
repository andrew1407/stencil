import { StencilElement, hostTag, define } from './base.js';
import { notify } from '../utils.js';
// ── Component: PWA install button (floating, bottom-right) ──────
// Self-contained install affordance. It stays hidden until the browser fires
// `beforeinstallprompt` (Chromium: app meets the install criteria and isn't
// installed yet); clicking it replays that deferred event to show the native
// install dialog. Hides itself once installed or already running standalone.
// Owns its own markup + behavior, so PWA concerns never touch DrawingApp.
export class StencilInstall extends StencilElement {
  #deferred = null;

  static inner() {
    return `<button type="button" id="installAppBtn" title="Install Stencil as an app on your device">⬇ Install app</button>`;
  }
  static template() { return hostTag('stencil-install', 'id="installHost" hidden', StencilInstall.inner()); }

  wire(_app) {
    // Already installed / launched as an app → never offer the install button.
    const standalone = matchMedia('(display-mode: standalone)').matches || navigator.standalone === true;
    if (standalone) return;

    const btn = this.querySelector('#installAppBtn');

    // The browser tells us the app is installable: stash the event and reveal.
    window.addEventListener('beforeinstallprompt', e => {
      e.preventDefault();          // suppress the default mini-infobar; we drive it
      this.#deferred = e;
      this.hidden = false;
    });

    // Installed (via our button or the browser's own UI) → tidy up.
    window.addEventListener('appinstalled', () => {
      this.#deferred = null;
      this.hidden = true;
      notify('Stencil installed', 'ok');
    });

    btn.addEventListener('click', async () => {
      if (!this.#deferred) return;
      this.#deferred.prompt();
      const { outcome } = await this.#deferred.userChoice;
      // A prompt can only be used once; drop it and hide regardless of choice.
      this.#deferred = null;
      this.hidden = true;
      if (outcome !== 'accepted') notify('Install dismissed', 'info');
    });
  }
}
define('stencil-install', StencilInstall);
