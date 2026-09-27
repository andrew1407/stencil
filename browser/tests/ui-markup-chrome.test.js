// Toolbar chrome: the logo ray wrap, the pinned status row, the points-row ring, the row
// menus, the selection separators and the no-native-title sweep. Split from ui-markup.test.js.
import { test } from 'node:test';
import assert from 'node:assert';

import { layout } from '../js/ui/layout.js';
import { LAYOUT_CSS, COMPONENTS_CSS } from './helpers/css.js';
import { createStubElement, installDom } from './helpers/dom.js';

// A stub DOM for one test: every id resolves to a fresh element, kept for the assertions.
const withDom = async (run) => {
    const doc = installDom({ autoCreateById: true }, {
        window: { addEventListener() {}, dispatchEvent() {}, innerWidth: 1280, innerHeight: 800 },
    });
    try { return await run(doc); } finally { doc.restore(); }
};

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
test('the status row is the same box with the incognito tag and without it', async () => {
    const css = LAYOUT_CSS;
    const row = css.slice(css.indexOf('\n.info {'), css.indexOf('}', css.indexOf('\n.info {')));
    // A FLEX row is what guarantees it: measured, an inline tag with vertical-align:middle still
    // stretched the line box by ~1.4px however tightly its own height was pinned.
    assert.match(row, /display: flex/, 'the row is not a line box');
    assert.match(row, /align-items: center/);
    // …with a floor so the empty and tagged states agree…
    assert.match(row, /line-height: 27px/);   // 27 + 2×10 padding: the desktop's 47px bar
    assert.match(row, /min-height: 27px/);
    // …and no second line to grow onto at a narrow width.
    assert.match(row, /white-space: nowrap/);
    assert.match(row, /overflow: hidden/);
    const tag = css.slice(css.indexOf('.info-incognito {'), css.indexOf('}', css.indexOf('.info-incognito {')));
    // The tag is locked to that same height rather than stretching it, and never grows
    // or shrinks as a flex item.
    assert.match(tag, /line-height: 27px/);
    assert.match(tag, /height: 27px/);
    assert.match(tag, /flex: 0 0 auto/);
    const sep = css.slice(css.indexOf('.info-divider {'), css.indexOf('}', css.indexOf('.info-divider {')));
    assert.match(sep, /flex: 0 0 auto/);
    // The glyph is a block, so it contributes no inline baseline/descender space — the
    // pixel-or-two that moved the canvas on the desktop.
    assert.match(css, /\.info-incognito \.ic \{ display: block; flex: 0 0 auto; \}/);
    // 13px inside a 20px line box: it cannot exceed what is reserved for it.
    const glyph = await withDom(async () => {
        const { updateInfo } = await import('../js/ui/projects/window/projectTitle.js');
        const info = createStubElement('span');
        document.getElementById = (id) => (id === 'image-info' ? info : null);
        updateInfo({ image: null, activeIsBlank: () => false, storage: { incognito: true } });
        return /<svg[^>]*width="(\d+)" height="(\d+)"/.exec(info.__infoParts.tag.innerHTML);
    });
    assert.ok(glyph && Number(glyph[1]) < 20 && Number(glyph[2]) < 20,
        `the glyph (${glyph?.[1]}px) must fit the line box`);
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
test('the bar separators part it in four, and the fill one follows its group', async () => {
    const { StencilSelectionPanel, showSelectionPanel } = await import('../js/ui/panel/selectionPanel.js');
    const inner = StencilSelectionPanel.inner();
    assert.equal((inner.match(/class="sel-sep"/g) || []).length, 4, 'four separators');
    // The fill group's own one is identified, starts hidden, and is toggled with the group.
    assert.match(inner, /id="sel-fill-sep"[^>]*style="display:none;"/);
    const shown = await withDom((doc) => {
        const [group, sep] = ['sel-fill-group', 'sel-fill-sep'].map((id) => doc.getElementById(id));
        group.style.display = sep.style.display = 'none';
        const app = { pointSize: 4, syncFsSelectionPanel() {}, renderLinesList() {} };
        const states = [];
        for (const locked of [true, false]) {
            showSelectionPanel(app, { color: '#ff0000', thickness: 2, locked, points: [] });
            states.push([group.style.display, sep.style.display]);
        }
        return states;
    });
    // …through the same reveal the group itself uses: the separator never shows or hides alone.
    assert.deepEqual(shown, [['flex', 'block'], ['none', 'none']], 'shown with the group, and hidden with it');
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
});

test('the custom tooltip shows data-tip / data-title, and never the native title', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const shown = await withDom(async (doc) => {
    const { initTooltips } = await import('../js/ui/tip/controlTooltip.js');
    initTooltips();
    // closest() matches a list of bare [attr] selectors, the way the tooltip asks.
    const control = (attrs) => {
      const el = createStubElement('button', { isConnected: true });
      for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, v);
      el.dataset = Object.fromEntries(Object.entries(attrs).filter(([k]) => k.startsWith('data-'))
        .map(([k, v]) => [k.slice(5), v]));
      el.closest = (sel) => (sel.split(',').some((s) => el.hasAttribute(s.trim().slice(1, -1))) ? el : null);
      return el;
    };
    const hover = (el) => {
      doc.dispatch('pointerover', { target: el, clientX: 5, clientY: 5 });
      t.mock.timers.tick(5000);
      const tip = doc.body.children.find((c) => c.id === 'app-tooltip');
      const text = tip?.classList.contains('visible') ? tip.innerHTML : '';
      doc.dispatch('pointerdown', {});
      return text;
    };
    return [{ title: 'native' }, { 'data-title': 'Titled' }, { 'data-tip': 'Tipped', title: 'native' }].map((a) => hover(control(a)));
  });
  assert.equal(shown[0], '', 'a native title alone shows nothing');
  assert.match(shown[1], /Titled/, 'data-title is shown');
  assert.match(shown[2], /Tipped/, 'data-tip is shown');
  assert.doesNotMatch(shown.join(''), /native/, 'the native text never reaches the tooltip');
});
