// A null-object DOM for booting the whole DrawingApp under Node: every lookup answers, every call
// is a no-op that answers again, so the constructor's UI wiring runs through without a rig per
// control. Window listeners are captured so a test can fire one; `restore()` puts the globals back.
const NULL_LINKS = new Set(['parentElement', 'parentNode', 'nextElementSibling', 'previousElementSibling',
  'firstElementChild', 'lastElementChild', 'offsetParent', 'host']);
const NUMBERS = new Set(['width', 'height', 'clientWidth', 'clientHeight', 'offsetWidth', 'offsetHeight',
  'scrollTop', 'scrollLeft', 'scrollWidth', 'scrollHeight', 'left', 'top', 'right', 'bottom', 'x', 'y',
  'childElementCount', 'devicePixelRatio', 'innerWidth', 'innerHeight', 'length']);
const STRINGS = new Set(['value', 'textContent', 'innerHTML', 'className', 'id', 'tagName', 'hash',
  'search', 'pathname', 'href']);
const FLAGS = new Set(['disabled', 'checked', 'hidden']);

const answer = () => new Proxy(function () {}, {
  get(_t, k) {
    if (k === Symbol.toPrimitive) return (hint) => (hint === 'number' ? 0 : '');
    if (k === Symbol.iterator) return function* () {};
    if (typeof k === 'symbol' || k === 'then') return undefined;
    if (k === 'closest' || k === 'querySelector') return () => null;
    if (NULL_LINKS.has(k)) return null;
    if (NUMBERS.has(k)) return 0;
    if (STRINGS.has(k)) return '';
    if (FLAGS.has(k)) return false;
    return answer();
  },
  set: () => true,
  apply: () => answer(),
  construct: () => answer(),
});

const GLOBALS = ['document', 'window', 'location', 'history', 'localStorage', 'BroadcastChannel'];

export const installNullDom = () => {
  const saved = Object.fromEntries(GLOBALS.map((k) => [k, globalThis[k]]));
  const listeners = {};
  const win = answer();
  globalThis.document = answer();
  globalThis.window = new Proxy(win, {
    get: (t, k) => (k === 'addEventListener' ? (type, fn) => { (listeners[type] ||= []).push(fn); } : t[k]),
  });
  globalThis.location = { hash: '', pathname: '/app', search: '' };
  globalThis.history = { replaceState() {} };
  globalThis.localStorage = { getItem: () => null, setItem() {}, removeItem() {}, key: () => null, length: 0 };
  // A live channel would hold the test process open.
  globalThis.BroadcastChannel = undefined;
  return {
    fire: (type, detail) => { for (const fn of listeners[type] || []) fn({ type, detail }); },
    restore: () => { for (const [k, v] of Object.entries(saved)) globalThis[k] = v; },
  };
};
