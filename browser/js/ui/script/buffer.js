// The one .stc the page is editing. The script window and the context menu's flyout are two
// views of THIS text, never of each other's DOM, so they cannot drift apart. Session only:
// a script is untrusted text the user may have pasted, so nothing persists it — no storage,
// no project field — and a reload starts empty.
let text = '';
const views = new Set();

export const scriptText = () => text;

// `from` is the view that already shows `next`; it is not called back over its own caret.
export const setScriptText = (next, from = null) => {
  text = typeof next === 'string' ? next : '';
  for (const view of views) if (view !== from) view(text);
};

// Returns the drop: a view outliving its DOM would repaint a node that is no longer there.
export const subscribeScript = (view) => { views.add(view); return () => views.delete(view); };
