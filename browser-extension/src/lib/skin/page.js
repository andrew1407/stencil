// What a session skin touches on a page: the glyphs and marks it swaps and the favicon it repaints.
// Shared by lib/webcore/ and lib/aqua/, which only supply the art. Browser twin: js/ui/skin/glyphs.js.
export const GLYPHS = 'svg.ic, svg.logo, img.logo';
export const MARKS = 'svg.logo, img.logo';
const NAME_RE = /^ic-([a-z0-9-]+)$/;

// The elements `selector` names at or under `root`; none where `root` cannot be searched.
export const glyphsAt = (root, selector = GLYPHS) => (root?.querySelectorAll
  ? [...(root.matches?.(selector) ? [root] : []), ...root.querySelectorAll(selector)] : []);

// 'logo' for a mark, else the first ic-* class `has` knows an art for; undefined without one.
export const glyphOf = (el, has) => (el.classList.contains('logo') ? 'logo'
  : [...el.classList].map((c) => NAME_RE.exec(c)?.[1]).find((n) => n && has(n)));

export const setFavicon = (doc, svg) => {
  const link = doc.querySelector?.('link[rel="icon"]');
  if (link && svg) link.href = `data:image/svg+xml,${encodeURIComponent(svg)}`;
};
