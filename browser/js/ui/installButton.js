import { StencilElement, hostTag, define } from './base.js';
import { wireHoverDust } from './motion.js';
import { notify, detectDesktopOS } from '../utils.js';
import INSTALL from '../config/installConfig.json' with { type: 'json' };
import { icon } from './icons.js';
// ── Component: install/download affordance (floating, bottom-right) ──
// Hover/focus menu with two ways to get Stencil: Install web app (PWA — shown only
// after `beforeinstallprompt`, hidden once installed) and Download desktop app
// (OS-specific zip from installConfig.json). On touch the desktop download is
// meaningless, so the button collapses to a direct PWA install (matching CSS) and
// stays hidden until the browser reports installability.
// ── Deferred install prompt, captured at MODULE-EVALUATION time ──────────────
// Chrome fires `beforeinstallprompt` on its own schedule; a listener added at
// `stencil:ready` can MISS it, and a missed BIP is gone for the page's lifetime.
// Module evaluation is the earliest moment this file controls, so the stash lives here.
let deferredPrompt = null;
const promptWatchers = new Set();
if (typeof window !== 'undefined') {
  window.addEventListener('beforeinstallprompt', (e) => {
    // No preventDefault(): Chrome logs a "Banner not shown" notice on every
    // load when the prompt is deferred; stashing alone keeps our button working.
    deferredPrompt = e;
    for (const fn of promptWatchers) fn();
  });
}

export class StencilInstall extends StencilElement {
  // Desktop download entry ({label, url}) for the current OS, or the fallback.
  static #desktop() {
    const d = INSTALL.desktop;
    const os = detectDesktopOS();
    return (os && d[os]) || d.fallback;
  }

  static inner() {
    // The desktop row is an <a> (it downloads a file), and the shared hover shimmer is
    // keyed on `button` and friends — so it takes the `.shimmer` opt-in explicitly, or it
    // would be the one row in the menu that does not light up under the pointer.
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
    // The menu is shown by a :hover / :focus-within rule alone, so its sand is wired
    // here rather than hung off an open/close (js/ui/motion.js wireHoverDust).
    wireHoverDust(this, this.querySelector('#install-menu'));
    // Touch device: the button IS the PWA install (no menu / desktop option), so it
    // only makes sense while installable — hide it until `beforeinstallprompt`.
    const mobile = matchMedia('(hover: none) and (pointer: coarse)').matches;
    if (mobile) this.hidden = true;

    // Already installed / launched as an app → desktop download still applies,
    // but never offer the PWA install option.
    const standalone = matchMedia('(display-mode: standalone)').matches || navigator.standalone === true;
    if (standalone) return;

    // Reveal/hide the PWA option (desktop: the menu row; mobile: the whole
    // button) from the module stash — including a BIP that fired BEFORE wire().
    const sync = () => {
      const installable = !!deferredPrompt;
      pwaBtn.hidden = !installable;
      if (mobile) this.hidden = !installable;
    };
    promptWatchers.add(sync);
    sync();

    // Installed (via our button or the browser's own UI) → tidy up.
    window.addEventListener('appinstalled', () => {
      deferredPrompt = null;
      sync();
      notify('Stencil installed', 'ok');
    });

    // Replay the deferred install prompt. Shared by the menu's PWA item (desktop)
    // and a direct tap on the button (mobile).
    const install = async () => {
      if (!deferredPrompt) return;
      const prompt = deferredPrompt;
      // A prompt can only be used once; drop it and hide regardless of choice.
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
