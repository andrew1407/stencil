import { StencilElement, hostTag, define } from '../base.js';
import { wireHoverDust } from '../motion.js';
import { notify, detectDesktopOS } from '../../utils.js';
import INSTALL from '../../config/installConfig.json' with { type: 'json' };
import { icon } from '../icons.js';
// Install/download affordance: Install web app (PWA, only once `beforeinstallprompt` fired,
// hidden once installed) and Download desktop app (installConfig.json). On touch the button
// collapses to a direct PWA install. The prompt is stashed at module evaluation: a listener
// added at `stencil:ready` can miss it, and a missed BIP is gone for the page's lifetime.
let deferredPrompt = null;
const promptWatchers = new Set();
if (typeof window !== 'undefined') {
  window.addEventListener('beforeinstallprompt', (e) => {
// No preventDefault(): Chrome would log a "Banner not shown" notice on every load.
    deferredPrompt = e;
    for (const fn of promptWatchers) fn();
  });
}

export class StencilInstall extends StencilElement {
  static #desktop() {
    const d = INSTALL.desktop;
    const os = detectDesktopOS();
    return (os && d[os]) || d.fallback;
  }

  static inner() {
// The desktop row is an <a>, so it opts into `.shimmer` explicitly (the shared hover
// shimmer is keyed on `button` and friends).
    const { label, url } = StencilInstall.#desktop();
    return `<div id="install-menu" role="menu" aria-label="Get Stencil">
        <button type="button" id="install-pwa-btn" role="menuitem" hidden>
          <span class="install-ic">${icon('download')}</span><span>Install web app (PWA)</span>
        </button>
        <a id="install-desktop-btn" class="shimmer" role="menuitem" href="${url}" download rel="noopener"
           data-title="Download the Stencil desktop app for ${label}">
          <span class="install-ic">${icon('monitor')}</span><span>Download desktop app</span>
        </a>
      </div>
      <button type="button" id="install-toggle" aria-haspopup="true"
              aria-label="Get Stencil — install or download">${icon('download')}</button>`;
  }
  static template() { return hostTag('stencil-install', 'id="install-host"', StencilInstall.inner()); }

  wire(_app) {
    const pwaBtn = this.querySelector('#install-pwa-btn');
    const toggle = this.querySelector('#install-toggle');
// The menu is shown by :hover / :focus-within alone, so its sand is wired here; the host is
// the hover region but the sand comes out of the icon.
    wireHoverDust(this, this.querySelector('#install-menu'), { anchor: toggle });
// Touch: the button IS the PWA install, so it only shows while installable.
    const mobile = matchMedia('(hover: none) and (pointer: coarse)').matches;
    if (mobile) this.hidden = true;

// Already installed: never offer the PWA option.
    const standalone = matchMedia('(display-mode: standalone)').matches || navigator.standalone === true;
    if (standalone) return;

// Including a BIP that fired before wire().
    const sync = () => {
      const installable = !!deferredPrompt;
      pwaBtn.hidden = !installable;
      if (mobile) this.hidden = !installable;
    };
    promptWatchers.add(sync);
    sync();

    window.addEventListener('appinstalled', () => {
      deferredPrompt = null;
      sync();
      notify('Stencil installed', 'ok');
    });

// Shared by the menu's PWA item (desktop) and a direct tap on the button (mobile).
    const install = async () => {
      if (!deferredPrompt) return;
      const prompt = deferredPrompt;
      // A prompt can only be used once.
      deferredPrompt = null;
      sync();
      prompt.prompt();
      const { outcome } = await prompt.userChoice;
      if (outcome !== 'accepted') notify('Install dismissed', 'info');
    };
    pwaBtn.addEventListener('click', install);
    if (mobile) toggle.addEventListener('click', install);
  }
}
define('stencil-install', StencilInstall);
