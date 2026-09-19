// Shared rig for the chatRowMenu.test.js family: a DOM-lite tree with listeners and parent
// tracking, and one wired transcript carrying a single settled row.

// A minimal live DOM: parent/child tracking (contains/remove), listeners with
// fire(), className/style/dataset — enough for the menu's build + delegation.
export const makeEl = (tag = 'div') => {
  const el = {
    tagName: String(tag).toUpperCase(), children: [], parentNode: null,
    className: '', dataset: {}, style: {}, value: '', offsetWidth: 120, offsetHeight: 100,
    _text: '', _html: '', _listeners: {},
    set textContent(v) { this._text = String(v); this.children.length = 0; },
    get textContent() { return this._text || this.children.map((c) => c.textContent).join(''); },
    set innerHTML(v) { this._html = String(v); },
    get innerHTML() { return this._html; },
    setAttribute() {},
    select() {},
    appendChild(c) { c.parentNode = el; el.children.push(c); return c; },
    append(...cs) { for (const c of cs) el.appendChild(c); },
    remove() {
      const kids = el.parentNode?.children;
      const i = kids ? kids.indexOf(el) : -1;
      if (i >= 0) kids.splice(i, 1);
      el.parentNode = null;
    },
    contains(node) { return node === el || el.children.some((c) => c.contains?.(node)); },
    closest(sel) {
      for (let n = el; n; n = n.parentNode) {
        if (String(n.className).split(/\s+/).includes(sel.slice(1))) return n;
      }
      return null;
    },
    querySelector: () => null,
    addEventListener(t, fn) { (el._listeners[t] ||= []).push(fn); },
    removeEventListener() {},
    fire(t, ev = {}) { for (const fn of el._listeners[t] || []) fn(ev); },
  };
  return el;
};

export const docListeners = [];
export const stubDom = () => {
  docListeners.length = 0;
  const body = makeEl('body');
  globalThis.document = {
    createElement: makeEl,
    body,
    getElementById: () => null,   // notify() no-ops against this
    addEventListener: (t, fn, cap) => docListeners.push({ t, fn, cap }),
    removeEventListener: (t, fn, cap) => {
      const i = docListeners.findIndex((l) => l.t === t && l.fn === fn && !!l.cap === !!cap);
      if (i >= 0) docListeners.splice(i, 1);
    },
  };
  globalThis.window = {
    innerWidth: 1024, innerHeight: 768,
    addEventListener: () => {}, removeEventListener: () => {},
  };
  return { body };
};

export const walk = (el, out = []) => { out.push(el); for (const c of el.children) walk(c, out); return out; };
export const byClass = (root, cls) => walk(root).filter((e) => String(e.className).split(/\s+/).includes(cls));
export const menuOn = (body) => byClass(body, 'chat-row-menu')[0] || null;
export const itemLabels = (menu) => byClass(menu, 'chat-row-menu-item').map((b) => b.children[0]?.textContent);
export const clickItem = (menu, label) => byClass(menu, 'chat-row-menu-item')
  .find((b) => b.children[0]?.textContent === label)
  .fire('click', { stopPropagation() {} });


// with the given hooks; returns what a right-click on the row needs.
export const wiredRow = async (row, hooks, tag = '') => {
  const { body } = stubDom();
  const { wireChatRowMenu } = await import(`../../js/ui/chatView.js?rowmenu${tag}`);
  const transcript = makeEl();
  const rowEl = makeEl();
  rowEl.className = `chat-msg chat-msg-${row?.role || 'assistant'}`;
  rowEl._chatRow = row;
  transcript.appendChild(rowEl);
  wireChatRowMenu(transcript, hooks);
  const rightClick = () => {
    const ev = { target: rowEl, clientX: 40, clientY: 60, prevented: false,
      preventDefault() { this.prevented = true; }, stopPropagation() {} };
    transcript.fire('contextmenu', ev);
    return ev;
  };
  return { body, transcript, rowEl, rightClick };
};
