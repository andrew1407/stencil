// What a drag will do, read while it is still in the air: 'open' (an image or a .stencil —
// save or incognito), 'layout' (a .json) or 'script' (a .stc). Desktop twin: dropSources.cpp
// dropKindOf. Mid-drag a browser hides file names, so only the MIME type is read there.

const ext = (name) => String(name || '').toLowerCase().split('.').pop();

export const dropKindOfFile = (file) => {
  if (!file) return 'open';
  const e = ext(file.name);
  if (e === 'stc') return 'script';
  if (e === 'json' || file.type === 'application/json') return 'layout';
  return 'open';
};

// Mid-drag: `types` = dataTransfer.types, `itemTypes` = each item's MIME. A .stencil and a .stc
// both come through as '' — that reads 'open', and a .stc still runs from either half.
export const dropKindOfDrag = (types, itemTypes = []) => {
  if (!types || !types.includes('Files')) return 'open';
  return itemTypes[0] === 'application/json' ? 'layout' : 'open';
};
