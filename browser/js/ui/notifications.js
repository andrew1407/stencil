import { StencilElement, hostTag, define } from './base.js';
import { icon } from './icons.js';
import { surfaceIn, surfaceOut, dockAwayPoint, retargetDust, SURFACE_MENU_IN_MS } from './motion.js';
// The bottom-left notification stack; utils.js `notify()` delegates here. Each message is
// its own .notify-toast child, newest at the bottom.

// Failures linger longer; clickable toasts (an action rides on them) longer still.
const FAIL_HIDE_MS = 3200;
const OK_HIDE_MS = 2400;
const CLICKABLE_HIDE_MS = 6000;
// Matches notifyLeave in css/animations/overlays.css.
const LEAVE_ANIM_MS = 260;
// 2x the shared menu clock: a passing notice can afford to drift.
const ENTER_DUST_MS = SURFACE_MENU_IN_MS * 2;   // 680
// Shorter than the entrance: a departure has nothing left to look at, and a longer one
// left the final grains crawling after the toast was gone.
const LEAVE_DUST_MS = 420;
// Desktop parity: Notifications::kMaxVisible in desktop/src/support/notifications.cpp.
export const MAX_VISIBLE = 3;

// A whitespace-free run (a filename, a URL) longer than `max` gets a middle ellipsis.
export const squeezeLongTokens = (msg, max = 48) =>
  String(msg ?? '').split(/(\s+)/).map((tok) => {
    if (/\s/.test(tok) || tok.length <= max) return tok;
    const keep = Math.floor((max - 1) / 2);
    return tok.slice(0, keep) + '…' + tok.slice(-keep);
  }).join('');

// A left-docked chat owns everything left of --chat-inset-left (chatPanel.js updateNotifyInset).
const freeLeft = () => {
  try { return parseFloat(getComputedStyle(document.body).getPropertyValue('--chat-inset-left')) || 0; }
  catch { return 0; }
};

// Past the free area's left edge at the toast's own height; only 0.15 toast-widths past
// it (not dockAwayPoint's 1.2), or every grain is off screen within the exit's first beat
// (desktop notifications.cpp twin).
const TOAST_REACH = 0.15;
const toastDustPoint = (toast) => {
  const r = toast.getBoundingClientRect?.();
  if (!r || !(r.width > 0)) return dockAwayPoint(r, 'left');
  return { x: freeLeft() - r.width * TOAST_REACH, y: r.top + r.height / 2 };
};
// No curve override: one timing function drives travel and alpha, and the default
// ease-out is the only shape with no tail. Near-zero stagger: at toast size a wave just
// leaves stragglers, so the cloud ends with the flight.
const TOAST_LEAVE_STAGGER = 0.12;

// The cloud stays on the free side of the edge: clipped there, motes pour out from behind
// the panel instead of across the composer. Relative to the host's box, so re-applied on move.
const clipDustToFree = (toast) => {
  const host = toast?.__dustHost;
  const edge = freeLeft();
  if (!host?.style || !(edge > 0) || !toast.getBoundingClientRect) return;
  const r = toast.getBoundingClientRect();
  host.style.clipPath = `inset(-4000px -4000px -4000px ${Math.round(edge - r.left)}px)`;
};

export class StencilNotifications extends StencilElement {
  static inner() { return ''; }
  static template() { return hostTag('stencil-notifications', 'id="notify-balloon"', StencilNotifications.inner()); }

  // `onClick` makes the toast an affordance (longer linger). `key` marks a running status: a
  // new one with the same key replaces its predecessor instead of stacking.
  notify(msg, type = 'ok', { onClick = null, key = null } = {}) {
    msg = squeezeLongTokens(msg);
    // Coalesce first: an identical clickless message restarts its timer, no replayed entrance.
    if (!onClick) {
      const dup = this.#live().find((el) =>
        el.classList.contains(`notify-${type}`)
        && !el.classList.contains('notify-clickable')
        && el.querySelector('.notify-text')?.textContent === msg);
      if (dup) {
        clearTimeout(dup._hideTimer);
        dup._hideTimer = setTimeout(() => this.#dismiss(dup),
          type === 'fail' ? FAIL_HIDE_MS : OK_HIDE_MS);
        return;
      }
    }
    // Replaced outright: the exit animation would keep the old toast beside the new one.
    if (key) for (const el of this.#live()) {
      if (el.dataset.notifyKey !== key) continue;
      clearTimeout(el._hideTimer);
      el.remove();
    }
    // Cap before adding; only toasts still standing count.
    const live = this.#live();
    for (let i = 0; i < live.length + 1 - MAX_VISIBLE; i++) this.#dismiss(live[i]);

    const toast = document.createElement('div');
    toast.className = `notify-toast notify-${type}${onClick ? ' notify-clickable' : ''}`;
    toast.setAttribute('role', 'status');
    if (key) toast.dataset.notifyKey = key;
    toast.innerHTML = '<span class="notify-icon"></span><span class="notify-text"></span>';
    toast.querySelector('.notify-icon').innerHTML =
      icon(type === 'fail' ? 'x' : (type === 'info' ? 'info' : 'check'), { size: 16 });
    toast.querySelector('.notify-text').textContent = msg;
    if (onClick) {
      toast.addEventListener('click', () => {
        // Dismiss first: the handler may open a dialog, and the toast must not outlive it.
        this.#dismiss(toast);
        onClick();
      }, { once: true });
    }
    this.appendChild(toast);
    // A new row bumps every sibling still flying; drag their clouds along.
    for (const el of this.children) if (el !== toast) { retargetDust(el); clipDustToFree(el); }
    surfaceIn(toast, toastDustPoint(toast), { ms: ENTER_DUST_MS });
    clipDustToFree(toast);
    toast._hideTimer = setTimeout(() => this.#dismiss(toast),
      onClick ? CLICKABLE_HIDE_MS : (type === 'fail' ? FAIL_HIDE_MS : OK_HIDE_MS));
  }

  // Oldest first (DOM order is insertion order); the ones already leaving are filtered out.
  #live() {
    return [...this.children].filter((el) => !el.classList.contains('notify-leaving'));
  }

  // Idempotent: a toast retired early by the cap still has its auto-hide timer pending.
  #dismiss(toast) {
    if (!toast?.classList || toast.classList.contains('notify-leaving')) return;
    clearTimeout(toast._hideTimer);
    // .notify-leaving gives the exit its own motion instead of the entrance backwards.
    toast.classList.add('notify-leaving');
    toast.classList.remove('notify-clickable');
    surfaceOut(toast, toastDustPoint(toast), { ms: LEAVE_DUST_MS, delayScale: TOAST_LEAVE_STAGGER });
    clipDustToFree(toast);
    setTimeout(() => {
      toast.remove();
      // Removing a row shrinks the column too.
      for (const el of this.children) { retargetDust(el); clipDustToFree(el); }
    }, LEAVE_ANIM_MS);
  }
}
define('stencil-notifications', StencilNotifications);
