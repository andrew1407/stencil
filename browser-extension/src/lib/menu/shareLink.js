// A link that opens the Stencil app on a page's image or video (contracts/launch/launch-uri.md `source`,
// with `incognito` to edit without saving), for the "Copy Stencil link" menu items.

// Null for a source another browser cannot fetch (blob:, data:, file:…) or a base that is no http(s) URL.
export const shareLink = (base, source, { incognito = false } = {}) => {
  if (!/^https?:\/\//i.test(String(source || ''))) return null;
  let url;
  try { url = new URL(base); } catch { return null; }
  if (!/^https?:$/.test(url.protocol)) return null;
  url.searchParams.set('source', source);
  if (incognito) url.searchParams.set('incognito', 'true');
  return url.href;
};
