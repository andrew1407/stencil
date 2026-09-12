// Pure, DOM-free sort/order helpers for projectsModal.js. An "item" is
// { key, name (lowercased), date (epoch ms), isRemote (bool) }.

// name = server + local interleaved (default); manual = the per-session drag order.
export const SORT_MODES = ['name', 'local', 'server', 'date-desc', 'date-asc', 'manual'];

// Stable tiebreak: name, then newest, then key.
const cmpName = (a, b) => a.name.localeCompare(b.name) || (b.date - a.date) || a.key.localeCompare(b.key);

// Returns a new array. `order` is the manual key sequence (mode 'manual' only); keys absent
// from it fall to the end.
export const sortProjectItems = (items, mode, order = []) => {
  const arr = items.slice();
  if (mode === 'local') arr.sort((a, b) => (a.isRemote - b.isRemote) || cmpName(a, b));
  else if (mode === 'server') arr.sort((a, b) => (b.isRemote - a.isRemote) || cmpName(a, b));
  else if (mode === 'date-desc') arr.sort((a, b) => (b.date - a.date) || cmpName(a, b));
  else if (mode === 'date-asc') arr.sort((a, b) => (a.date - b.date) || cmpName(a, b));
  else if (mode === 'manual') {
    const pos = new Map(order.map((k, i) => [k, i]));
    arr.sort((a, b) => {
      const pa = pos.has(a.key) ? pos.get(a.key) : Infinity;
      const pb = pos.has(b.key) ? pos.get(b.key) : Infinity;
      return (pa - pb) || cmpName(a, b);
    });
  } else arr.sort(cmpName);
  return arr;
};

// Seeds from `base` (the existing manual order, else what the modal was showing), gives
// every current key a slot, then moves draggedKey before/after targetKey. Returns a new array.
export const reconcileManualOrder = (fullKeys, base, draggedKey, targetKey, before) => {
  let out = (base && base.length) ? base.slice() : fullKeys.slice();
  for (const k of fullKeys) if (!out.includes(k)) out.push(k);
  out = out.filter((k) => k !== draggedKey);
  let idx = out.indexOf(targetKey);
  if (idx < 0) idx = out.length - 1;
  out.splice(before ? idx : idx + 1, 0, draggedKey);
  return out;
};
