// Shared rig for the connectModal.test.js family: a wired StencilConnectModal over the DOM
// stub, plus the row/descendant readers its assertions walk the rendered list with.
import { StencilConnectModal } from '../../js/ui/connectModal.js';
import { createStubElement, installDom } from './dom.js';
import { layout } from '../../js/ui/layout.js';

// layout() transitively imports every ui component, including the connect modal.
export const markup = layout();
export const count = (needle) => markup.split(needle).length - 1;

export const WIRE_IDS = [
  'connect-modal-overlay', 'connect-url', 'connect-token', 'connect-add', 'connect-reconnect',
  'connect-list', 'connect-batch-bar', 'connect-batch-count', 'connect-batch-reconnect',
  'connect-batch-disconnect', 'connect-autoconnect', 'connect-sync',
  'connect-filter', 'connect-select-all', 'connect-batch-selected',
];


export const conn = (url, kind) => ({
  url, status: 'connected', connected: true, credentialKind: kind,
  mintInvite: async () => `${url}#token=t`,
});

// `reduced` false is for the motion tests only: everywhere else reduced motion makes a
// render synchronous, so the assertions read the settled list.
export const openModal = (conns, { reduced = true, mgrExtra = {}, appExtra = {} } = {}) => {
  const notes = [];   // what notify() posted, in order
  const doc = installDom({}, {
    window: createStubElement('window', { matchMedia: () => ({ matches: reduced }) }),
    matchMedia: () => ({ matches: reduced }),   // motion.js reads the bare global
    CSS: { escape: (s) => s },
  });
  const list = createStubElement('div', {
    querySelector: (sel) => {
      const m = /\[data-url="(.*)"\]/.exec(sel);
      return (m && list.children.find((c) => c.dataset?.url === m[1])) || null;
    },
  });
  // innerHTML = '' is how render() clears the list; the stub keeps children in an array.
  Object.defineProperty(list, 'innerHTML', { get: () => '', set: (v) => { if (!v) list.children.length = 0; } });
  // Select all carries its label in a <span> the modal re-titles (Select ↔ Deselect all) and
  // a glyph it swaps (check ↔ cross); the stub icon records the swap by class.
  const selectAllLabel = createStubElement('span');
  const selectAllIcon = createStubElement('svg');
  selectAllIcon.classList.add('ic', 'ic-check');
  Object.defineProperty(selectAllIcon, 'outerHTML', {
    set: (html) => { const m = /ic ic-([a-z]+)/.exec(html); selectAllIcon.classList.remove('ic-check', 'ic-x'); if (m) selectAllIcon.classList.add(`ic-${m[1]}`); },
    get: () => '',
  });
  for (const id of WIRE_IDS) {
    doc.register(id, id === 'connect-list' ? list
      : id === 'connect-select-all' ? createStubElement('button', {
        querySelector: (sel) => (sel === 'span' ? selectAllLabel : sel === '.ic' ? selectAllIcon : null) })
      : createStubElement('div'));
  }
  const selectAllGlyph = () => (selectAllIcon.classList.contains('ic-x') ? 'x' : selectAllIcon.classList.contains('ic-check') ? 'check' : null);
  const byUrl = new Map(conns.map((c) => [c.url, c]));
  const urls = conns.map((c) => c.url);
  // utils.notify() posts through #notify-balloon; recording it is how a test reads a toast.
  doc.register('notify-balloon', createStubElement('div', { notify: (m, t) => notes.push([m, t]) }));
  new StencilConnectModal().wire({
    connections: { knownUrls: urls, urls, expiredUrls: [], reconnectable: true,
      get: (u) => byUrl.get(u) ?? null, ...mgrExtra },
    ...appExtra,
  });
  doc.getElementById('connect-modal-overlay').__stencilModal.open();
  return { doc, list, notes, filter: doc.getElementById('connect-filter'), selectAllGlyph,
    modal: doc.getElementById('connect-modal-overlay').__stencilModal };
};

export const rows = (list) => list.children.filter((c) => c.classList?.contains('connect-row'));
export const rowUrls = (list) => rows(list).map((r) => r.dataset.url);
export const hasClass = (el, c) => el.classList.contains(c);
export const descendants = (el) => el.children.flatMap((c) => [c, ...(c.children ? descendants(c) : [])]);
export const find = (el, cls) => descendants(el).find((c) => c.classList?.contains(cls)) ?? null;

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
