// The "?" hints badge, the info line's incognito tag and the viewport-tracing incognito
// frame. Split from ui-markup.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import { layout } from '../../../js/ui/layout.js';
import { LAYOUT_CSS, COMPONENTS_CSS } from '../../helpers/css.js';
import { createStubElement, installDom } from '../../helpers/dom.js';

const markup = layout();

class StubObserver {
    constructor(fn) { this.fn = fn; StubObserver.all.push(this); }
    observe() {}
    disconnect() {}
}
StubObserver.all = [];
globalThis.MutationObserver = StubObserver;
globalThis.customElements = { define: () => {}, get: () => undefined };
globalThis.HTMLElement = class {};

const doc = installDom({ autoCreateById: true }, {
    matchMedia: () => ({ matches: false }),
    requestAnimationFrame: () => 1,
    cancelAnimationFrame: () => {},
    getComputedStyle: () => ({ getPropertyValue: () => '#7c3aed' }),
    window: { addEventListener: () => {}, removeEventListener: () => {}, dispatchEvent: () => {} },
    location: { hash: '', pathname: '/app', search: '' },
    history: { replaceState: () => {} },
});

const { StencilToolbar } = await import('../../../js/ui/toolbar/toolbar.js');
const { DrawingApp } = await import('../../../js/core/drawingApp.js');

// A collapsed toolbar with an image open, so the "?" bubble is live and carries its line.
const bubbleRig = () => {
    doc.getElementById('image-info').dataset.size = 'Image Size: 800 × 600 px';
    doc.body.classes.add('controls-collapsed');
    doc.body.classes.delete('incognito-mode');
    StubObserver.all.length = 0;
    const popup = doc.getElementById('hints-popup');
    popup.children.length = 0;
    const bar = Object.create(StencilToolbar.prototype);
    bar.querySelector = () => null;
    bar.querySelectorAll = () => [];
    bar.wire({});
    const badge = popup.children.find((c) => c.className === 'hints-incognito');
    return {
        badge,
        setIncognito: (on) => {
            doc.body.classList.toggle('incognito-mode', on);
            for (const o of StubObserver.all) o.fn();
        },
    };
};

// The status line, repainted the way the toggle repaints it — updateIncognitoUI, not updateInfo.
const infoRig = () => {
    const info = createStubElement('span', { id: 'image-info' });
    doc.register('image-info', info);
    const app = Object.assign(Object.create(DrawingApp.prototype), {
        image: {}, canvas: { width: 800, height: 600 }, lines: [], activeProjectId: null,
        activeIsBlank: () => false, storage: { incognito: false, temporary: true },
    });
    return {
        info,
        paint: (on) => { app.storage.incognito = on; app.updateIncognitoUI(); return info.__infoParts; },
    };
};

// The "?" badge has its own hover bubble (.hints-popup), so it carries neither a title nor a
// data-title and opts out of the shared tooltip — both at once would show the text twice.
test('the ? hints badge owns its bubble and opts out of the floating tooltip', () => {
    const badge = markup.slice(markup.indexOf('id="hints-btn"'));
    const openTag = badge.slice(0, badge.indexOf('>'));
    assert.ok(/data-no-tooltip/.test(openTag), '#hints-btn opts out of the shared tooltip');
    assert.ok(!/\stitle=/.test(openTag) && !/data-title=/.test(openTag),
        'no second copy of the shortcut text on the badge itself');
    const src = readFileSync(new URL('../../../js/ui/toolbar/toolbar.js', import.meta.url), 'utf8');
    assert.ok(!/hintsBtn\.title\s*=/.test(src), 'and the toggle handler must not put one back');
    const tt = readFileSync(new URL('../../../js/ui/tip/controlTooltip.js', import.meta.url), 'utf8');
    assert.ok(/data-no-tooltip/.test(tt), 'controlTooltip honours the opt-out');
});

// The "?" holds two facts only — the image size and, in incognito, that the session is never
// saved — and sits inside the project-name field so it reads as this project's.
test('the ? badge sits inside the project-name field, right after the name', () => {
    const field = markup.slice(markup.indexOf('class="project-name-field"'));
    const inField = field.slice(0, field.indexOf('id="controls-body"'));
    assert.ok(inField.indexOf('id="hints-btn"') > 0, 'the ? escaped the project-name field');
    assert.ok(inField.indexOf('id="project-name-input"') < inField.indexOf('id="hints-btn"'),
        'the ? must follow the name it belongs to');
    // The field shrink-wraps (no fixed 240px basis) — that is what makes "beside" true.
    const fieldTag = field.slice(0, field.indexOf('>'));
    assert.ok(!/flex:0 1 240px/.test(fieldTag), 'the fixed-width slot pushed the ? away again');
    assert.ok(/flex:0 1 auto/.test(fieldTag), 'the field no longer shrink-wraps its content');
});

test('the ? bubble carries the size and the incognito line — and nothing else', () => {
    const src = readFileSync(new URL('../../../js/ui/toolbar/toolbar.js', import.meta.url), 'utf8');
    // The old shortcut wall is gone; those hints live in the ℹ info modal (infoConfig.json).
    assert.ok(!/SHORTCUTS_HINT/.test(src), 'the shortcut wall is back in the bubble');
    assert.ok(!/Ctrl\+Scroll|Alt\+Scroll|for full help/.test(src), 'shortcut text is back in the bubble');
    const info = readFileSync(new URL('../../../js/config/infoConfig.json', import.meta.url), 'utf8');
    for (const hint of ['Ctrl + wheel', 'Alt + wheel', 'Ctrl + Shift + wheel'])
        assert.ok(info.includes(hint), `${hint} must still be documented in the info modal`);
    // Two facts: the #image-info size line, plus an incognito line off body.incognito-mode.
    assert.match(src, /sizeText\.nodeValue = live \? \(hasImage \? size : 'No image loaded'\) : '';/);
    assert.match(src, /hints-incognito/);
    assert.match(src, /incognito-mode/);
    // Shown when the facts mean something — an image is open…
    assert.match(src, /\/\^Image Size:\/\.test\(size\)/);
    // …or while incognito is on, image or not, and only while the toolbar is collapsed: the info
    // line under the open toolbar carries the same two facts.
    assert.match(src, /const live = \(hasImage \|\| incognito\) && collapsed;/);
    assert.match(src, /classList\.contains\('controls-collapsed'\)/);
    const css = LAYOUT_CSS;
    assert.match(css, /body\.controls-collapsed \.info \{ display: none; \}/,
      'the size line folds away with the rows it belongs to');
    // The bubble reads the info line's own text, never its incognito tag as well.
    assert.match(src, /el\.dataset\.size/);
});

// The mode has to read where the image facts are read, empty editor included: the info
// line carries its own tag (ui/projects/window/projectTitle.updateInfo), beside the "?" bubble's
// line. Both are asserted as RENDERED — what builds them is not the contract.
test('both incognito badges render the glyph and the wording, and stand hidden while it is off', () => {
    const { info, paint } = infoRig();
    const off = paint(false);
    assert.strictEqual(off.tag.className, 'info-incognito');
    assert.strictEqual(off.tag.style.display, 'none', 'the tag is hidden while the mode is off');
    assert.strictEqual(off.sep.style.display, 'none', 'and no divider dangles without it');
    const on = paint(true);
    assert.notStrictEqual(on.tag.style.display, 'none', 'incognito shows the tag');
    // The app's own glyph, not an emoji.
    assert.match(on.tag.innerHTML, /class="ic ic-incognito"/);
    assert.ok(!/🕶/.test(on.tag.innerHTML), 'the emoji is gone from the info line');
    assert.match(on.tag.innerHTML, /Incognito — not saved/);
    assert.strictEqual(info.dataset.size, 'Image Size: 800 × 600 px', 'the size stays readable on its own');

    // …and the "?" bubble states the same thing, off the same one flag.
    const { badge, setIncognito } = bubbleRig();
    assert.strictEqual(badge.style.display, 'none', 'the bubble line waits for the mode too');
    setIncognito(true);
    assert.notStrictEqual(badge.style.display, 'none');
    assert.match(badge.innerHTML, /class="ic ic-incognito"/);
    assert.ok(!/🕶/.test(badge.innerHTML), 'the emoji is gone from the bubble too');
    assert.match(badge.innerHTML, /Incognito — not saved/);
    const css = LAYOUT_CSS;
    assert.match(css, /\.info-incognito \{/, 'and it is styled like the bubble line');
    // The glyph sits on the words, and the divider is muted + unselectable.
    const tag = css.slice(css.indexOf('.info-incognito {'), css.indexOf('}', css.indexOf('.info-incognito {')));
    assert.match(tag, /display: inline-flex/);
    assert.match(tag, /gap: 5px/);
    assert.match(tag, /color: var\(--accent\)/, 'the glyph inherits this via currentColor');
    const div = css.slice(css.indexOf('.info-divider {'), css.indexOf('}', css.indexOf('.info-divider {')));
    assert.match(div, /color: var\(--text-muted\)/, 'muted, not competing with the facts');
    assert.match(div, /user-select: none/);
    const hints = css.slice(css.indexOf('.hints-incognito {'), css.indexOf('}', css.indexOf('.hints-incognito {')));
    assert.match(hints, /display: flex/);
    assert.match(hints, /gap: 5px/);
});

// The frame belongs to the EDITOR: it traces the whole visible canvas region, picture and
// empty ground, at any zoom — round the image alone it reads as a selection (user report).
test('the incognito frame traces the canvas VIEWPORT, not the picture', () => {
    const markup = readFileSync(new URL('../../../js/ui/panel/mainContent.js', import.meta.url), 'utf8');
    // It lives in the VIEWPORT (the scrollport), not in the shrink-wrapping container.
    const vpAt = markup.indexOf('id="canvas-viewport"');
    const frameAt = markup.indexOf('class="incognito-frame"');
    const containerAt = markup.indexOf('id="canvas-container"');
    assert.ok(vpAt > -1 && frameAt > vpAt && frameAt < containerAt,
        'the frame is a child of the viewport, ahead of the canvas container');
    const css = COMPONENTS_CSS;
    const frame = css.slice(css.indexOf('.incognito-frame {'), css.indexOf('}', css.indexOf('.incognito-frame {')));
    // Sticky: an absolute box scrolls away with the content, which is what the frame must
    // NOT do — the viewport edge is the same edge however far the picture is scrolled.
    assert.match(frame, /position: sticky/);
    assert.match(frame, /top: 0/);
    assert.match(frame, /left: 0/);
    // Sized in PIXELS from the viewport's own measurements: a percentage resolves against
    // the scrollable content, i.e. the image's box at the current zoom.
    assert.match(frame, /width: var\(--vp-w, 100%\)/);
    assert.match(frame, /height: var\(--vp-h, 100%\)/);
    // …and its height is given back, so nothing in the viewport shifts.
    assert.match(frame, /margin-bottom: calc\(-1 \* var\(--vp-h, 0px\)\)/);
    assert.match(frame, /pointer-events: none/);
    // The publisher: the viewport's INNER box (less any scrollbar), on resize only —
    // sticky already handles scrolling, so nothing runs per scroll frame.
    assert.match(markup, /setProperty\('--vp-w', `\$\{vp\.clientWidth\}px`\)/);
    assert.match(markup, /setProperty\('--vp-h', `\$\{vp\.clientHeight\}px`\)/);
    assert.match(markup, /new ResizeObserver\(syncFrameBox\)\.observe\(vp\)/);
    assert.ok(!/addEventListener\('scroll'[^)]*syncFrameBox/.test(markup), 'no per-scroll work');
    // The old container-stretching workaround is gone with the container dependency.
    assert.ok(!css.includes('body.incognito-mode.canvas-empty .canvas-container'),
        'the empty-state stretch is obsolete now the frame owns the viewport');
    const layout = LAYOUT_CSS;
    const base = layout.slice(layout.indexOf('\n.canvas-container {'), layout.indexOf('}', layout.indexOf('\n.canvas-container {')));
    // …and the container still shrink-wraps the canvas: as a flex item, `flex: none` with
    // no width of its own is the inline-block's replacement (see canvasCentering.test.js).
    assert.ok(/flex: none/.test(base) && !/width:/.test(base),
        'and the container still shrink-wraps the canvas, untouched');
});
