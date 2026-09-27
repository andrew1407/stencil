// One page-wide subtree observer for every pass that upgrades what renders later — the numeric
// fields' expressions, the controls' accessible names — instead of one observer each.
const added = [];
const retipped = [];
let observer = null;

const observe = (root) => {
  if (observer) return;
  observer = new MutationObserver((records) => {
    for (const rec of records) {
      if (rec.type === 'attributes') { for (const fn of retipped) fn(rec.target); continue; }
      for (const node of rec.addedNodes) {
        if (node.nodeType !== 1) continue;
        for (const fn of added) fn(node);
      }
    }
  });
  observer.observe(root, { childList: true, subtree: true, attributes: true, attributeFilter: ['data-title', 'data-tip'] });
};

/** `fn` sees every element added under `root` from now on, in registration order. */
export const onElementAdded = (fn, root = document.body) => { added.push(fn); observe(root); };

/** `fn` sees every element whose data-title or data-tip changes. */
export const onTipChanged = (fn, root = document.body) => { retipped.push(fn); observe(root); };
