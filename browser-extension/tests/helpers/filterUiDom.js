// The f-* control set src/lib/filterUi.js reads, plus a pill box that parses its own innerHTML —
// shared by the filterUi*.test.js suites.
export const control = (value = '', checked = false) => ({ value, checked, textContent: '' });
export const stubDom = () => {
  const els = {
    'f-search': control(' cat '), 'f-regex': control('', true),
    'f-minw': control('10'), 'f-maxw': control(''), 'f-minh': control('abc'), 'f-maxh': control('200'),
    'f-img': control('', true), 'f-bg': control('', false), 'f-video': control('', true),
    'f-poster': control('', false), 'f-meta': control('', true),
    'f-fmt-toggle': control(),
  };
  const box = {
    inputs: [],
    set innerHTML(html) {
      this.inputs = [...html.matchAll(/value="([^"]+)"/g)].map((m) => ({
        value: m[1], checked: true, handlers: [],
        addEventListener: function (t, fn) { if (t === 'change') this.handlers.push(fn); },
      }));
    },
    get innerHTML() { return ''; },
    querySelectorAll: function () { return this.inputs; },
  };
  els['f-formats'] = box;
  return { doc: { getElementById: (id) => els[id] || null }, els, box };
};
