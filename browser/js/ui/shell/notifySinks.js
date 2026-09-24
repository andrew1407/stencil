// The two ways a notice reaches the user, behind one shape — `show(msg, type, opts)`, true
// when delivered — so notifications.js picks one by notifyChannel() and never knows which.
// Desktop twin: support/notify/NotificationSink.hpp (ToastStack, SystemNotifier).

const TITLE = 'Stencil';

// The in-app stack: hands the notice to the <stencil-notifications> element's own toast().
export class ToastSink {
  #stack;
  constructor(stack) { this.#stack = stack; }
  isAvailable() { return true; }
  show(msg, type, opts) { this.#stack.toast(msg, type, opts); return true; }
}

// The browser's Notification API. `api` is injected so a test can stand in for the browser.
export class SystemSink {
  #api;
  #focus;
  constructor({ api = () => globalThis.Notification, focus = () => globalThis.focus?.() } = {}) {
    this.#api = api;
    this.#focus = focus;
  }

  // Granted permission is the whole gate: 'default' and 'denied' both fall back to the toasts.
  isAvailable() {
    const N = this.#api();
    return !!N && N.permission === 'granted';
  }

  // 'granted' | 'denied' | 'default' | 'unsupported'; asked from the user's own click.
  static async requestPermission(api = () => globalThis.Notification) {
    const N = api();
    if (!N) return 'unsupported';
    if (N.permission === 'granted') return 'granted';
    try { return await N.requestPermission(); } catch { return 'denied'; }
  }

  // `key` becomes the tag, so a running status replaces its predecessor as the toasts do;
  // a click brings the page back and runs the toast's own action.
  show(msg, type = 'ok', { onClick = null, key = null } = {}) {
    if (!this.isAvailable()) return false;
    const N = this.#api();
    let n;
    try { n = new N(TITLE, { body: String(msg ?? ''), tag: key || undefined }); }
    catch { return false; }
    n.onclick = () => {
      this.#focus();
      n.close?.();
      onClick?.();
    };
    return true;
  }
}

// The sink keyed by the channel's own name; one that cannot deliver hands over to the toasts.
export const pickSink = (channel, sinks) => {
  const sink = sinks[channel] ?? sinks.toast;
  return sink.isAvailable() ? sink : sinks.toast;
};
