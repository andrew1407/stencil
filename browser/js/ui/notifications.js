import { StencilElement, hostTag, define } from './base.js';
// ── Component: bottom-left notification balloon ─────────────────
// Owns the show/auto-hide logic; utils.js `notify()` delegates to this.

// Auto-hide delays: failures linger a bit longer so they're not missed.
const FAIL_HIDE_MS = 3200;
const OK_HIDE_MS = 2400;

export class StencilNotifications extends StencilElement {
  #timer = null;

  static inner() {
    return `
        <span class="notify-icon"></span>
        <span class="notify-text"></span>
    `;
  }
  static template() { return hostTag('stencil-notifications', 'id="notifyBalloon"', StencilNotifications.inner()); }

  notify(msg, type = 'ok') {
    const icon = this.querySelector('.notify-icon');
    const text = this.querySelector('.notify-text');
    icon.textContent = type === 'fail' ? '✗' : (type === 'info' ? 'ℹ' : '✓');
    text.textContent = msg;
    this.classList.remove('notify-ok', 'notify-fail', 'notify-info');
    this.classList.add('notify-' + type);
    this.classList.add('notify-visible');
    clearTimeout(this.#timer);
    this.#timer = setTimeout(() => this.classList.remove('notify-visible'),
      type === 'fail' ? FAIL_HIDE_MS : OK_HIDE_MS);
  }
}
define('stencil-notifications', StencilNotifications);
