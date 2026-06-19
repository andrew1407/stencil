// Accent (brand-colour) presets — the theme-colour choices offered in the
// Visuals modal (🎨). The first, violet, is the default brand colour; every
// preset is just one primary hex, because --accent-2 (the darker/lighter
// hover-active shade) and the focus/selection glows all derive from --accent via
// color-mix() in css/theme.css. Mirrors the extension (extension/src/lib/
// accent.js) and desktop (theme.cpp accentPresets) preset lists.
export const ACCENTS = [
  { key: 'violet',  label: 'Violet',      hex: '#7c3aed' },
  { key: 'pink',    label: 'Pink',        hex: '#ec4899' },
  { key: 'yellow',  label: 'Yellow',      hex: '#eab308' },
  { key: 'orange',  label: 'Orange',      hex: '#ea580c' },
  { key: 'crimson', label: 'Crimson',     hex: '#be123c' },
  { key: 'aqua',    label: 'Aqua',        hex: '#0891b2' },
  { key: 'sky',     label: 'Sky blue',    hex: '#0ea5e9' },
  { key: 'blue',    label: 'Blue',        hex: '#2563eb' },
  { key: 'grass',   label: 'Grass green', hex: '#16a34a' },
  { key: 'green',   label: 'Green',       hex: '#047857' },
  { key: 'brown',   label: 'Brown',       hex: '#a87c50' },
  { key: 'grey',    label: 'Grey',        hex: '#64748b' },
];

// localStorage key for the chosen accent; same flavour as drawingApp_theme.
export const ACCENT_STORAGE_KEY = 'drawingApp_accent';
export const DEFAULT_ACCENT = 'violet';
export const isAccent = (key) => ACCENTS.some((a) => a.key === key);
// Primary hex for an accent key (falls back to the first preset — violet).
export const accentHex = (key) =>
  (ACCENTS.find((a) => a.key === key) || ACCENTS[0]).hex;

// The app favicon as an SVG string, with the panel outline painted in `hex` (the
// rest is fixed brand art). Mirrors favicon.svg — kept in sync by hand.
export const faviconSvg = (hex) =>
  '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">' +
  '<rect x="2" y="2" width="60" height="60" rx="13" fill="#2b2f3a"/>' +
  `<rect x="2.75" y="2.75" width="58.5" height="58.5" rx="12.25" fill="none" stroke="${hex}" stroke-width="1.5"/>` +
  '<rect x="12" y="12" width="40" height="40" rx="4" fill="#3a3f4b"/>' +
  '<polyline points="16,46 27,24 38,38 50,18" fill="none" stroke="#FFFF00" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>' +
  '<g fill="#FFFF00" stroke="#000000" stroke-width="1.25">' +
  '<circle cx="16" cy="46" r="3.4"/><circle cx="27" cy="24" r="3.4"/><circle cx="38" cy="38" r="3.4"/><circle cx="50" cy="18" r="3.4"/>' +
  '</g></svg>';

// Repaint the browser-tab favicon (and the PWA status-bar theme-color) to the
// chosen accent. The favicon is a static .svg file the browser can't read our CSS
// var from, so we swap the <link> to an inline data-URL SVG carrying the colour.
export const applyAccentFavicon = (key) => {
  if (typeof document === 'undefined') return;
  const hex = accentHex(isAccent(key) ? key : DEFAULT_ACCENT);
  let link = document.querySelector('link[rel="icon"]');
  if (!link) {
    link = document.createElement('link');
    link.rel = 'icon';
    document.head.appendChild(link);
  }
  link.type = 'image/svg+xml';
  link.href = 'data:image/svg+xml,' + encodeURIComponent(faviconSvg(hex));
  const meta = document.querySelector('meta[name="theme-color"]');
  if (meta) meta.setAttribute('content', hex);
};
