// A window's picture for the genie (genie/flight.js), which the DOM offers no API for: a clone with
// every computed style inlined, drawn through an SVG foreignObject onto a canvas. Never shown, never
// read back, so a tainted canvas costs nothing. Resolves null when the picture cannot be made.
const XHTML = 'http://www.w3.org/1999/xhtml', SVG_NS = 'http://www.w3.org/2000/svg';
const PSEUDOS = Object.freeze(['::before', '::after', '::placeholder']);
// Still and whole wherever its flight has it; uncapped, as a % cap read its own width and shrank a bubble under its text (user report).
const ROOT_STYLE = 'transform:none;animation:none;transition:none;opacity:1;clip-path:none;margin:0;'
  + 'position:relative;left:0;top:0;right:auto;bottom:auto;translate:none;scale:none;rotate:none;max-width:none;max-height:none;';
// Parts a stylesheet paints through the engine's own pseudo-elements (a slider's track and thumb, a
// colour well, a scroller's bar), which no computed style reports: their rules are carried instead.
const PART = /::-webkit-(slider-|color-swatch|scrollbar)/;
const partRules = () => {
  const out = [];
  const walk = (rules) => {
    for (const r of rules) {
      if (r.selectorText && PART.test(r.selectorText)) out.push(r);
      else if (r.cssRules && (!r.conditionText || r.constructor.name !== 'CSSMediaRule'
               || globalThis.matchMedia?.(r.conditionText).matches)) walk(r.cssRules);
    }
  };
  for (const sheet of document.styleSheets) {
    try { walk(sheet.cssRules); } catch { /* a cross-origin sheet: none of ours */ }
  }
  return out;
};
// `var(--x, fallback)` written out from `cs`, innermost first, as the picture has no custom properties.
const resolveVars = (text, cs) => {
  let out = text;
  for (let guard = 0; guard < 32 && out.includes('var('); guard++) {
    out = out.replace(/var\(\s*(--[\w-]+)\s*(?:,\s*([^()]*))?\)/g, (_, name, fallback) => {
      const v = cs.getPropertyValue(name).trim();
      return v || (fallback ?? '').trim();
    });
  }
  return out;
};
// The rules among `rules` that paint a part of `src`, retargeted to its copy, `key`.
const partsCss = (src, key, rules, cs) => {
  const out = [];
  for (const r of rules) {
    for (const sel of r.selectorText.split(',')) {
      const at = sel.indexOf('::');
      if (at < 0 || !PART.test(sel)) continue;
      let hit = false;
      try { hit = src.matches(sel.slice(0, at).trim() || '*'); } catch { hit = false; }
      if (hit) out.push(`[data-genie="${key}"]${sel.slice(at).trim()}{${resolveVars(r.style.cssText, cs)}}`);
    }
  }
  return out;
};
// The longhands that shape a still picture, as the browser lists them (one it lacks is dropped);
// motion, cursors and scrolling are never seen in it. Custom properties are already resolved.
const SEEN = Object.freeze(['display', 'position', 'box-sizing', 'float', 'clear', 'visibility', 'opacity', 'z-index',
  'top', 'right', 'bottom', 'left', 'width', 'height', 'min-width', 'min-height', 'max-width', 'max-height',
  'margin-top', 'margin-right', 'margin-bottom', 'margin-left',
  'padding-top', 'padding-right', 'padding-bottom', 'padding-left',
  'border-top-width', 'border-right-width', 'border-bottom-width', 'border-left-width',
  'border-top-style', 'border-right-style', 'border-bottom-style', 'border-left-style',
  'border-top-color', 'border-right-color', 'border-bottom-color', 'border-left-color',
  'border-top-left-radius', 'border-top-right-radius', 'border-bottom-right-radius', 'border-bottom-left-radius',
  'outline-width', 'outline-style', 'outline-color', 'outline-offset', 'box-shadow',
  'background-color', 'background-image', 'background-size', 'background-position-x', 'background-position-y',
  'background-repeat', 'background-clip', 'background-origin', 'background-blend-mode',
  'color', 'font-family', 'font-size', 'font-weight', 'font-style', 'font-variant', 'font-stretch',
  'font-feature-settings', 'font-variant-numeric', 'line-height', 'letter-spacing', 'word-spacing',
  'text-align', 'text-indent', 'text-transform', 'text-decoration-line', 'text-decoration-color',
  'text-decoration-style', 'text-shadow', 'text-overflow', 'white-space', 'word-break', 'overflow-wrap',
  'vertical-align', 'direction', 'writing-mode', '-webkit-text-fill-color', '-webkit-line-clamp',
  '-webkit-box-orient', 'list-style-type', 'list-style-position', 'tab-size',
  'flex-direction', 'flex-wrap', 'flex-grow', 'flex-shrink', 'flex-basis', 'order',
  'justify-content', 'align-items', 'align-content', 'align-self', 'justify-items', 'justify-self',
  'row-gap', 'column-gap', 'grid-template-columns', 'grid-template-rows', 'grid-auto-flow',
  'grid-auto-columns', 'grid-auto-rows', 'grid-column-start', 'grid-column-end', 'grid-row-start', 'grid-row-end',
  'overflow-x', 'overflow-y', 'clip-path', 'filter', 'backdrop-filter', 'mix-blend-mode', 'isolation',
  'transform', 'transform-origin', 'object-fit', 'object-position', 'aspect-ratio', 'table-layout',
  'border-collapse', 'border-spacing', 'caption-side', 'empty-cells', 'appearance', 'accent-color',
  'caret-color', 'content', 'quotes', 'counter-reset', 'counter-increment', 'image-rendering',
  'mask-image', 'mask-size', 'mask-position', 'mask-repeat', 'mask-mode', 'mask-composite',
  '-webkit-mask-image', '-webkit-mask-size', '-webkit-mask-position', '-webkit-mask-repeat',
  'fill', 'fill-opacity', 'fill-rule', 'stroke', 'stroke-width', 'stroke-opacity', 'stroke-linecap',
  'stroke-linejoin', 'stroke-dasharray', 'stroke-dashoffset', 'stop-color', 'stop-opacity', 'shape-rendering']);
const longhands = (cs) => {
  const listed = new Set();
  for (let i = 0; i < cs.length; i++) listed.add(cs[i]);
  return SEEN.filter((k) => listed.has(k));
};
const valuesOf = (cs, names) => names.map((k) => cs.getPropertyValue(k));

// The browser's own value of every longhand per tag, read in a blank standards-mode frame no page
// sheet reaches; cached for the page's life. A frame that cannot be made leaves every value written.
const UA_DEFAULTS = new Map();
const uaDefaults = (tag, names, frameRef) => {
  if (UA_DEFAULTS.has(tag)) return UA_DEFAULTS.get(tag);
  let out = null;
  try {
    if (!frameRef.frame) {
      const frame = document.createElement('iframe');
      frame.setAttribute('aria-hidden', 'true');
      frame.style.cssText = 'position:fixed;width:0;height:0;border:0;visibility:hidden;pointer-events:none';
      document.body.appendChild(frame);
      frame.contentDocument.open();
      frame.contentDocument.write('<!doctype html><html><body></body></html>');
      frame.contentDocument.close();
      frameRef.frame = frame;
    }
    const doc = frameRef.frame.contentDocument;
    const probe = doc.createElement(tag);
    doc.body.appendChild(probe);
    out = valuesOf(frameRef.frame.contentWindow.getComputedStyle(probe), names);
    probe.remove();
  } catch { out = null; }
  UA_DEFAULTS.set(tag, out);
  return out;
};

// Only what the picture cannot work out for itself: a value is left out when it is both the browser's
// own for the tag and its parent's, since inherited or not it then resolves to it there too.
const styleText = (names, vals, ua, parentVals) => {
  let out = '';
  for (let i = 0; i < names.length; i++) {
    if (ua && parentVals && ua[i] === vals[i] && parentVals[i] === vals[i]) continue;
    out += `${names[i]}:${vals[i]};`;
  }
  return out;
};

// What a clone lacks, by tag: a field's live value, a box's own pixels, an image queued (`pending`) for inlineImg.
const LIVE_STATE = Object.freeze({
  INPUT: (from, to) => {
    if (from.type === 'checkbox' || from.type === 'radio') to.toggleAttribute('checked', from.checked);
    else to.setAttribute('value', from.value);
  },
  TEXTAREA: (from, to) => { to.textContent = from.value; },
  SELECT: (from, to) => { [...to.options].forEach((o, i) => o.toggleAttribute('selected', i === from.selectedIndex)); },
  CANVAS: (from, to) => {
    try {
      const img = Object.assign(document.createElement('img'), { src: from.toDataURL() });
      img.setAttribute('style', to.getAttribute('style') || '');
      to.replaceWith(img);
    } catch { /* tainted: the band shows the box's background there */ }
  },
  IMG: (from, to, pending) => { if (!/^data:/.test(from.currentSrc || from.src)) pending?.push([from, to]); },   // not yet pixels
});
const copyLiveState = (from, to, pending = null) => {
  if (/^(INPUT|SELECT|TEXTAREA|BUTTON)$/.test(from.tagName)) to.style.margin = getComputedStyle(from).margin;   // the image reverts it
  LIVE_STATE[from.tagName]?.(from, to, pending);
};
const inlineImg = async ([from, to]) => {
  await Promise.race([from.decode?.().catch(() => {}), new Promise((ok) => setTimeout(ok, 250))]);
  to.removeAttribute('srcset');
  if (!from.naturalWidth) { to.removeAttribute('src'); return; }   // not loaded: the box's background, never a broken mark
  const c = Object.assign(document.createElement('canvas'), { width: from.naturalWidth, height: from.naturalHeight });
  try { c.getContext('2d').drawImage(from, 0, 0); to.setAttribute('src', c.toDataURL()); } catch { to.removeAttribute('src'); }   // tainted: the same
};

const isOneLine = (src, cs) => [...src.childNodes].some((n) => n.nodeType === 3 && n.nodeValue.trim())
  && src.clientHeight - parseFloat(cs.paddingTop) - parseFloat(cs.paddingBottom) < 1.6 * (parseFloat(cs.lineHeight) || 1.25 * parseFloat(cs.fontSize));
// `el` is measured and styled as it stands; scroll offsets are not kept (the band shows its top).
export function cloneStyled(el, pending = null) {
  const clone = el.cloneNode(true);
  const from = [el, ...el.querySelectorAll('*')];
  const to = [clone, ...clone.querySelectorAll('*')];
  const names = longhands(getComputedStyle(el));
  const vals = new Map();   // element -> its computed values, in `names` order
  const frameRef = { frame: null };
  const sheetParts = partRules();
  const rules = [];
  from.forEach((src, i) => {
    const dst = to[i];
    // An icon's own gradients keep their ids: its url(#…) fills resolve in the picture's own document.
    if (!dst) return; if (src.namespaceURI !== SVG_NS) dst.removeAttribute('id');
    const cs = getComputedStyle(src);
    const mine = valuesOf(cs, names);
    vals.set(src, mine);
    // The box itself writes everything: its parent in the picture is a bare wrapper.
    const parentVals = i === 0 ? null : vals.get(src.parentElement);
    dst.setAttribute('style', styleText(names, mine, uaDefaults(src.localName, names, frameRef), parentVals));
    // The picture lays text a hair wider: bars, rows and text the real box keeps on one line stay so.
    if (/auto|scroll/.test(cs.overflowX) && src.scrollWidth <= src.clientWidth + 1) dst.style.overflowX = 'hidden';
    if (/auto|scroll/.test(cs.overflowY) && src.scrollHeight <= src.clientHeight + 1) dst.style.overflowY = 'hidden';
    const gutter = src.offsetWidth - src.clientWidth - parseFloat(cs.borderLeftWidth) - parseFloat(cs.borderRightWidth);
    if (/auto|scroll/.test(cs.overflowY + cs.overflowX) && gutter < 1) dst.style.scrollbarWidth = 'none';
    const [f, l] = [src.firstElementChild?.getBoundingClientRect(), src.lastElementChild?.getBoundingClientRect()];
    if (cs.flexWrap === 'wrap' && f && l.top < f.bottom) dst.style.flexWrap = 'nowrap';   // one line, centred or not
    if (cs.whiteSpace === 'normal' && src.getClientRects().length === 1 && isOneLine(src, cs)) dst.style.whiteSpace = 'nowrap';
    for (const pseudo of PSEUDOS) {
      const ps = getComputedStyle(src, pseudo);
      if (pseudo === '::placeholder' ? src.placeholder && !src.value : ps.content && ps.content !== 'none' && ps.content !== 'normal') {
        dst.setAttribute('data-genie', String(i));
        rules.push(`[data-genie="${i}"]${pseudo}{${styleText(names, valuesOf(ps, names), null, null)}}`);
      }
    }
    const parts = sheetParts.length ? partsCss(src, i, sheetParts, cs) : [];
    if (parts.length) { dst.setAttribute('data-genie', String(i)); rules.push(...parts); }
  });
  frameRef.frame?.remove();
  from.forEach((src, i) => { if (to[i]) copyLiveState(src, to[i], pending); });
  clone.setAttribute('style', clone.getAttribute('style') + ROOT_STYLE);
  return { clone, css: rules.join('\n') };
}

// Resolves a canvas holding `el` as it shows (its on-screen rect, at `scale` device px per CSS px), or null;
// the clone lays out at the box's own size, fitted to the rect, so a box a transform scales keeps its shape.
export async function snapshot(el, { scale = globalThis.devicePixelRatio || 1 } = {}) {
  try {
    const r = el?.getBoundingClientRect?.();
    if (!(r?.width >= 1 && r?.height >= 1)) return null;
    const w = Math.ceil(el.offsetWidth || r.width), h = Math.ceil(el.offsetHeight || r.height);
    const pending = [], { clone, css } = cloneStyled(el, pending);
    await Promise.all(pending.map(inlineImg));
    const wrap = document.createElementNS(XHTML, 'div');
    const style = document.createElementNS(XHTML, 'style');
    style.textContent = css;
    wrap.append(style, clone);
    const body = new XMLSerializer().serializeToString(wrap);
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}">`
      + `<foreignObject x="0" y="0" width="100%" height="100%">${body}</foreignObject></svg>`;
    const img = new Image();
    img.src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(svg)}`;
    await img.decode();
    const canvas = document.createElement('canvas');
    canvas.width = Math.round(r.width * scale);
    canvas.height = Math.round(r.height * scale);
    canvas.getContext('2d').drawImage(img, 0, 0, canvas.width, canvas.height);
    return canvas;
  } catch {
    return null;
  }
}
