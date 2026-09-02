import { StencilElement, hostTag, define } from './base.js';
import { icon } from './icons.js';
import { surfaceIn, surfaceOut, dockAwayPoint, retargetDust, SURFACE_MENU_IN_MS } from './motion.js';
// ── Component: bottom-left notification stack ───────────────────
// Owns the show/auto-hide logic; utils.js `notify()` delegates to this.
// #notify-balloon is the STACK, not a toast: each message gets its own .notify-toast
// child, newest at the bottom, growing upward — a burst shows every message.

// Auto-hide delays: failures linger a bit longer so they're not missed; clickable
// toasts (an action rides on them) linger longer still.
const FAIL_HIDE_MS = 3200;
const OK_HIDE_MS = 2400;
const CLICKABLE_HIDE_MS = 6000;
// How long .notify-leaving stays on — matches notifyLeave in css/animations.css.
const LEAVE_ANIM_MS = 260;
// Toast dust, 2x the shared menu clock's length — a passing notice can afford to drift
// rather than snap.
const ENTER_DUST_MS = SURFACE_MENU_IN_MS * 2;   // 680
const LEAVE_DUST_MS = 1040;
// The stack never grows past this (desktop parity: Notifications::kMaxVisible in
// desktop/src/support/notifications.cpp). Past three the column starts walling off the
// side of the canvas, and the oldest message is the one nobody is still reading.
export const MAX_VISIBLE = 3;

// Squeeze any whitespace-free run (a filename, a URL) longer than `max` with a middle
// ellipsis, so a toast stays a small balloon instead of a wall — full names belong in
// tooltips and lists, not transient messages. Pure — unit-tested.
export const squeezeLongTokens = (msg, max = 48) =>
  String(msg ?? '').split(/(\s+)/).map((tok) => {
    if (/\s/.test(tok) || tok.length <= max) return tok;
    const keep = Math.floor((max - 1) / 2);
    return tok.slice(0, keep) + '…' + tok.slice(-keep);
  }).join('');

// The dust's origin/target: off the left edge, at the toast's own (bottom-of-stack) height.
const toastDustPoint = (toast) => dockAwayPoint(toast.getBoundingClientRect?.(), 'left');

export class StencilNotifications extends StencilElement {
  // The stack starts empty; every toast is created by notify().
  static inner() { return ''; }
  static template() { return hostTag('stencil-notifications', 'id="notify-balloon"', StencilNotifications.inner()); }

  // `onClick` makes the toast an affordance (longer linger): clicking runs it and
  // dismisses. `key` marks a running STATUS rather than an event: a new one with the
  // same key replaces its predecessor instead of stacking under it.
  notify(msg, type = 'ok', { onClick = null, key = null } = {}) {
    msg = squeezeLongTokens(msg);
    // Coalesce FIRST: an identical clickless message already standing never stacks —
    // its hide timer restarts and the entrance is NOT replayed (the element stays
    // put), turning a zoom burst's per-step "Saved" chatter into one calm toast.
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
    // Replaced OUTRIGHT, not dismissed: the exit animation would keep the old toast
    // beside the new one for its whole 260ms. (Same key + same text coalesced above;
    // this replace is for a keyed status whose MESSAGE changed.)
    if (key) for (const el of this.#live()) {
      if (el.dataset.notifyKey !== key) continue;
      clearTimeout(el._hideTimer);
      el.remove();
    }
    // Cap BEFORE adding, so the stack is never over the limit even for a frame. Oldest
    // first, and only toasts still standing count — one already playing its exit is on
    // its way out and must not push a live one off the stack.
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
    // Appended last → bottom of the column, which is where the eye already is.
    this.appendChild(toast);
    // Adding a row to the flex column bumps every sibling already flying (a burst
    // firing on one tick, before either finished its own entrance) — drag their clouds
    // along rather than leaving them stranded at the box they were grabbed at.
    for (const el of this.children) if (el !== toast) retargetDust(el);
    surfaceIn(toast, toastDustPoint(toast), { ms: ENTER_DUST_MS });
    toast._hideTimer = setTimeout(() => this.#dismiss(toast),
      onClick ? CLICKABLE_HIDE_MS : (type === 'fail' ? FAIL_HIDE_MS : OK_HIDE_MS));
  }

  // Toasts on screen, oldest first. DOM order is insertion order here — nothing
  // reorders these children — and the ones already leaving are filtered out.
  #live() {
    return [...this.children].filter((el) => !el.classList.contains('notify-leaving'));
  }

  // Play a toast out and remove it. Idempotent: a toast retired early by the cap still
  // has its own auto-hide timer pending, and that must not restage the exit.
  #dismiss(toast) {
    if (!toast?.classList || toast.classList.contains('notify-leaving')) return;
    clearTimeout(toast._hideTimer);
    // .notify-leaving gives the exit its own motion (drop away + shrink) instead of
    // replaying the springy entrance backwards.
    toast.classList.add('notify-leaving');
    toast.classList.remove('notify-clickable');
    surfaceOut(toast, toastDustPoint(toast), { ms: LEAVE_DUST_MS });
    setTimeout(() => {
      toast.remove();
      // Removing a row shrinks the column too — the same retarget, the other direction.
      for (const el of this.children) retargetDust(el);
    }, LEAVE_ANIM_MS);
  }
}
define('stencil-notifications', StencilNotifications);
