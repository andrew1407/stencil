// Toolbar chrome: the logo ray wrap, the pinned status row, the points-row ring, the row
// menus, the selection separators and the no-native-title sweep. Split from ui-markup.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import { layout } from '../js/ui/layout.js';
import { LAYOUT_CSS, COMPONENTS_CSS } from './helpers/css.js';

const markup = layout();
const count = (needle) => markup.split(needle).length - 1;

// The logo's hover rays live on ::before of .app-logo-wrap because SVG elements host no
// pseudo-elements, so the wrap must exist once and enclose the .app-logo svg.
test('the app logo sits inside its ray-layer wrap, with the accent menu beside it', () => {
    assert.strictEqual(count('class="app-logo-wrap"'), 1, 'one .app-logo-wrap');
    assert.strictEqual(count('class="app-logo"'), 1, 'one .app-logo');
    const wrapAt = markup.indexOf('class="app-logo-wrap"');
    const logoAt = markup.indexOf('class="app-logo"', wrapAt);
    const menuAt = markup.indexOf('class="accent-dd-menu logo-accent-menu"', wrapAt);
    const closeAt = markup.indexOf('</span>', wrapAt);
    assert.ok(wrapAt !== -1 && logoAt !== -1 && menuAt !== -1 && closeAt !== -1,
        'wrap, logo, accent menu and closing tag present');
    assert.ok(wrapAt < logoAt && logoAt < menuAt && menuAt < closeAt,
        '.app-logo and .logo-accent-menu are children of .app-logo-wrap');
    // Starts hidden and empty (picker.js fills it lazily on first open).
    const menuTag = markup.slice(menuAt, markup.indexOf('>', menuAt));
    assert.ok(menuTag.includes('hidden'), 'the accent menu ships hidden');
});

// The incognito tag is passive decor: both states are the identical box, pinned in CSS, so
// toggling it moves no pixel (user report on the desktop's equivalent label).
test('the status row is the same box with the incognito tag and without it', () => {
    const css = LAYOUT_CSS;
    const row = css.slice(css.indexOf('\n.info {'), css.indexOf('}', css.indexOf('\n.info {')));
    // A FLEX row is what guarantees it: measured, an inline tag with vertical-align:middle still
    // stretched the line box by ~1.4px however tightly its own height was pinned.
    assert.match(row, /display: flex/, 'the row is not a line box');
    assert.match(row, /align-items: center/);
    // …with a floor so the empty and tagged states agree…
    assert.match(row, /line-height: 20px/);
    assert.match(row, /min-height: 20px/);
    // …and no second line to grow onto at a narrow width.
    assert.match(row, /white-space: nowrap/);
    assert.match(row, /overflow: hidden/);
    const tag = css.slice(css.indexOf('.info-incognito {'), css.indexOf('}', css.indexOf('.info-incognito {')));
    // The tag is locked to that same height rather than stretching it, and never grows
    // or shrinks as a flex item.
    assert.match(tag, /line-height: 20px/);
    assert.match(tag, /height: 20px/);
    assert.match(tag, /flex: 0 0 auto/);
    const sep = css.slice(css.indexOf('.info-divider {'), css.indexOf('}', css.indexOf('.info-divider {')));
    assert.match(sep, /flex: 0 0 auto/);
    // The glyph is a block, so it contributes no inline baseline/descender space — the
    // pixel-or-two that moved the canvas on the desktop.
    assert.match(css, /\.info-incognito \.ic \{ display: block; flex: 0 0 auto; \}/);
    // 13px inside a 20px line box: it cannot exceed what is reserved for it.
    const app = readFileSync(new URL('../js/ui/projects/window/projectTitle.js', import.meta.url), 'utf8');
    const size = /icon\('incognito', \{ size: (\d+) \}\)/.exec(app);
    assert.ok(size && Number(size[1]) < 20, `the glyph (${size?.[1]}px) must fit the line box`);
    // …and nothing compensates by resizing the canvas: the frame is an overlay, and no
    // rule keys the viewport off the incognito state.
    const comp = COMPONENTS_CSS;
    assert.ok(!/body\.incognito-mode[^{]*\.canvas-(viewport|container)[^{]*\{[^}]*(height|width|margin|padding)/.test(comp),
        'incognito must not resize the canvas to make room');
});

// An outline paints outside the border box, where the sticky column header covers it, so the
// points-row ring is inset by its own width — as the desktop's delegate does.
test('the points-table row ring is drawn inside the row, not around it', () => {
    const css = LAYOUT_CSS;
    for (const cls of ['row-highlighted', 'row-focused']) {
        const rule = css.slice(css.indexOf(`.coordinates-table tbody tr.${cls} {`),
                               css.indexOf('}', css.indexOf(`.coordinates-table tbody tr.${cls} {`)));
        assert.match(rule, /outline: 2px solid/, `${cls} still rings the row`);
        assert.match(rule, /outline-offset: -2px/, `${cls} draws that ring inside the row`);
    }
    // …and the header really is the thing that would cover it, so the rule earns its keep.
    assert.match(css, /\.coordinates-table thead th \{[^}]*position: sticky/);
});

// The project-row "…" menu is content-sized, with a smaller floor for short menus and a cap
// so a long label cannot run away.
test('the row menus are sized to their content, not to a wide floor', () => {
    const css = COMPONENTS_CSS;
    const rule = css.slice(css.indexOf('.project-menu, .chat-row-menu {'),
                           css.indexOf('}', css.indexOf('.project-menu, .chat-row-menu {')));
    assert.match(rule, /width: max-content/, 'the menu hugs its widest row');
    const floor = Number(/min-width: (\d+)px/.exec(rule)[1]);
    assert.ok(floor <= 150, `the floor (${floor}px) must not exceed what the items need`);
    assert.match(rule, /max-width: min\(/, 'and a long label is capped rather than unbounded');
});

// The selected-line bar is parted into header | colours | geometry | fill | actions by
// hairlines in its own amber; the fill group's separator comes and goes with the group.
test('the bar separators part it in four, and the fill one follows its group', () => {
    const js = readFileSync(new URL('../js/ui/panel/selectionPanel.js', import.meta.url), 'utf8');
    const inner = js.slice(js.indexOf('static inner()'), js.indexOf('static template()'));
    assert.equal((inner.match(/class="sel-sep"/g) || []).length, 4, 'four separators');
    // The fill group's own one is identified, starts hidden, and is toggled with the group.
    assert.match(inner, /id="sel-fill-sep"[^>]*style="display:none;"/);
    // …through the same slide+dust the group itself uses (selectionPanelMotion covers the
    // motion; here it is only that the separator is driven WITH the group, never alone).
    const show = js.slice(js.indexOf('const fillSep ='), js.indexOf('const panel ='));
    assert.match(show, /revealControls\(fillSep, true, 'block'\)/, 'shown with the group');
    assert.match(show, /revealControls\(fillSep, false\)/, 'and hidden with it');
    // Deselect wears the bar's own amber token, not an orange literal of its own.
    const css = LAYOUT_CSS;
    const cta = css.slice(css.indexOf('.deselect-btn {'), css.indexOf('}', css.indexOf('.deselect-btn {')));
    assert.match(cta, /var\(--bg-sel-btn\)/, 'the same token its sibling buttons use');
    assert.ok(!/#e67e22/.test(cta), 'and no hardcoded orange left');
});

// No control in the app or the extension carries a native `title`, in markup or from code
// (user report: the mic showed both); text is data-title, composed text data-tip.
test('no native title attribute anywhere — the custom tooltip is the only tooltip', async () => {
  const { readFileSync, readdirSync, statSync } = await import('node:fs');
  const { join } = await import('node:path');
  const walk = (dir, out = []) => {
    for (const f of readdirSync(dir)) {
      const p = join(dir, f);
      if (statSync(p).isDirectory()) walk(p, out);
      else if (/\.(js|html)$/.test(f)) out.push(p);
    }
    return out;
  };
  const roots = [new URL('../js', import.meta.url).pathname, new URL('../../browser-extension/src', import.meta.url).pathname];
  const offenders = [];
  for (const file of roots.flatMap((r) => walk(r))) {
    const src = readFileSync(file, 'utf8');
    if (/[\s\n]title="/.test(src)) offenders.push(`${file}: title="`);
    // DOM setters and attribute writes; document.title (the tab) and parseTip's result object are not tooltips.
    for (const m of src.matchAll(/(?<![\w.])([A-Za-z_$][\w$]*)\.title = |setAttribute\('title'/g)) {
      if (m[1] === 'document' || m[1] === 'tip') continue;
      offenders.push(`${file}: ${m[0].trim()}`);
    }
  }
  assert.deepEqual(offenders, []);
  const ct = readFileSync(new URL('../js/ui/tip/controlTooltip.js', import.meta.url), 'utf8');
  assert.ok(ct.includes("closest('[data-tip], [data-title]')"), 'the tooltip listens for data attributes only');
  assert.ok(!ct.includes("getAttribute('title')"), 'and never reads the native one');
});
