// Outlines the one page element whose source matches a hovered list row. Independent of
// lib/highlight.js (its own attribute + style); the injected function is self-contained.

// True when some frame marked the element; false on a restricted page. A falsy
// `source` clears the outline everywhere.
export const highlightSourceOnTab = async (tabId, source, color) => {
  try {
    const results = await chrome.scripting.executeScript({
      target: { tabId, allFrames: true }, func: highlightPageElementForSource,
      args: [source || '', color || '#7c3aed'],
    });
    return results.some((r) => r && r.result);
  } catch {
    return false;
  }
};

const highlightPageElementForSource = (source, color = '#7c3aed') => {
  const STYLE_ID = 'stencil-listhover-style';
  const ATTR = 'data-stencil-listhover';

  document.querySelectorAll('[' + ATTR + ']').forEach((el) => el.removeAttribute(ATTR));
  if (!source) {
    const s = document.getElementById(STYLE_ID);
    if (s) s.remove();
    return false;
  }

  const abs = (raw) => { try { return new URL(raw, location.href).href; } catch { return ''; } };
  const want = abs(source);
  if (!want) return false;

  const bgUrls = (el, pseudo) => {
    const bg = getComputedStyle(el, pseudo).backgroundImage;
    const urls = [];
    if (bg && bg !== 'none') {
      const re = /url\((['"]?)(.*?)\1\)/g;
      let m;
      while ((m = re.exec(bg))) if (m[2]) urls.push(abs(m[2]));
    }
    return urls;
  };
  const matches = (el) => {
    if (!el.matches) return false;
    if (el.matches('img')) return abs(el.currentSrc || el.src) === want;
    if (el.matches('image')) return abs(el.getAttribute('href') || el.getAttribute('xlink:href')) === want;
    if (el.matches('video')) return abs(el.currentSrc || el.src) === want || (el.poster && abs(el.poster) === want);
    return bgUrls(el, null).includes(want) || bgUrls(el, '::before').includes(want) || bgUrls(el, '::after').includes(want);
  };

  let target = null;
  for (const el of document.querySelectorAll('img, svg image, video')) {
    if (matches(el)) { target = el; break; }
  }
  if (!target) {
    for (const el of document.querySelectorAll('*')) {
      if (matches(el)) { target = el; break; }
    }
  }
  if (!target) return false;

  if (!document.getElementById(STYLE_ID)) {
    const toRgb = (hex) => {
      let h = String(hex || '').trim().replace('#', '');
      if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
      const n = parseInt(h, 16);
      return Number.isFinite(n) && h.length === 6 ? { r: (n >> 16) & 255, g: (n >> 8) & 255, b: n & 255 } : { r: 124, g: 58, b: 237 };
    };
    const { r, g, b } = toRgb(color);
    const style = document.createElement('style');
    style.id = STYLE_ID;
    style.textContent =
      '@keyframes stencil-listhover-pulse{0%{box-shadow:0 0 0 0 rgba(' + r + ',' + g + ',' + b + ',.9),0 0 0 0 rgba(' + r + ',' + g + ',' + b + ',.35);}'
      + '100%{box-shadow:0 0 0 4px rgba(' + r + ',' + g + ',' + b + ',.85),0 0 22px 10px rgba(' + r + ',' + g + ',' + b + ',.35);}}'
      + '[' + ATTR + ']{outline:4px solid ' + color + ' !important;outline-offset:2px !important;'
      + 'box-shadow:0 0 0 4px rgba(' + r + ',' + g + ',' + b + ',.85),0 0 22px 10px rgba(' + r + ',' + g + ',' + b + ',.35) !important;'
      + 'animation:stencil-listhover-pulse .22s ease-out !important;'
      + 'transition:outline-color .12s ease,box-shadow .12s ease !important;}';
    (document.head || document.documentElement).appendChild(style);
  }
  target.setAttribute(ATTR, '');
  // Sweeping the list must not thrash the page's scroll position.
  const r = target.getBoundingClientRect();
  const inView = r.bottom > 0 && r.right > 0 && r.top < innerHeight && r.left < innerWidth;
  if (!inView) {
    try { target.scrollIntoView({ block: 'center', inline: 'center', behavior: 'smooth' }); } catch { /* older engine */ }
  }
  return true;
};
