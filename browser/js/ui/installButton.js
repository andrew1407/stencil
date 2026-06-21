import { StencilElement, hostTag, define } from './base.js';
import { notify, detectDesktopOS } from '../utils.js';
import INSTALL from '../config/installConfig.json' with { type: 'json' };
import { icon } from './icons.js';
// ── Component: install/download affordance (floating, bottom-right) ──
// Hover/focus menu with two ways to get Stencil:
//   • Install web app (PWA) — shown only after `beforeinstallprompt` fires; click
//     replays that deferred event. Hidden once installed/standalone.
//   • Download desktop app — OS-specific release zip (installConfig.json), falling
//     back to the releases page when the platform is unknown.
// On a touch device ((hover:none) and (pointer:coarse)) the desktop download is
// meaningless, so we collapse all of this to a single affordance: the button is the
// PWA install (no menu, no desktop option — see the matching CSS that un-sticks it to
// sit in the normal flow at the bottom of the page and hides the menu). It stays
// hidden until the browser reports the app is installable (`beforeinstallprompt`), so
// it never shows as a dead button, and a tap then triggers the install directly.
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
          <span class="install-ic">${icon('download')}</span><span>Install web app (PWA)</span>
        </button>
        <a id="install-desktop-btn" role="menuitem" href="${url}" download rel="noopener"
           title="Download the Stencil desktop app for ${label}">
          <span class="install-ic">${icon('monitor')}</span><span>Download desktop app</span>
        </a>
      </div>
      <button type="button" id="install-toggle" aria-haspopup="true"
              title="Get Stencil — install or download">${icon('download')}</button>`;
  }
  static template() { return hostTag('stencil-install', 'id="install-host"', StencilInstall.inner()); }

  wire(_app) {
    const pwaBtn = this.querySelector('#install-pwa-btn');
    const toggle = this.querySelector('#install-toggle');
    // Touch device: the button IS the PWA install (no menu / desktop option), so it
    // only makes sense while installable — hide it until `beforeinstallprompt`.
    const mobile = matchMedia('(hover: none) and (pointer: coarse)').matches;
    if (mobile) this.hidden = true;

    // Already installed / launched as an app → desktop download still applies,
    // but never offer the PWA install option.
    const standalone = matchMedia('(display-mode: standalone)').matches || navigator.standalone === true;
    if (standalone) return;

    // The browser tells us the app is installable: stash the event and reveal the
    // PWA option in the menu (desktop) / the whole button (mobile — it's the install).
    window.addEventListener('beforeinstallprompt', e => {
      e.preventDefault();          // suppress the default mini-infobar; we drive it
      this.#deferred = e;
      pwaBtn.hidden = false;
      if (mobile) this.hidden = false;
    });

    // Installed (via our button or the browser's own UI) → tidy up.
    window.addEventListener('appinstalled', () => {
      this.#deferred = null;
      pwaBtn.hidden = true;
      if (mobile) this.hidden = true;
      notify('Stencil installed', 'ok');
    });

    // Replay the deferred install prompt. Shared by the menu's PWA item (desktop)
    // and a direct tap on the button (mobile).
    const install = async () => {
      if (!this.#deferred) return;
      this.#deferred.prompt();
      const { outcome } = await this.#deferred.userChoice;
      // A prompt can only be used once; drop it and hide regardless of choice.
      this.#deferred = null;
      pwaBtn.hidden = true;
      if (mobile) this.hidden = true;
      if (outcome !== 'accepted') notify('Install dismissed', 'info');
    };
    pwaBtn.addEventListener('click', install);
    if (mobile) toggle.addEventListener('click', install);
  }
}
define('stencil-install', StencilInstall);
