// The "?" hints badge and the info line's incognito tag, as rendered and as wired.
// Split from ui-markup.test.js; the viewport-tracing frame is incognitoFrame.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import { layout } from '../../../js/ui/layout.js';
import { LAYOUT_CSS } from '../../helpers/css.js';
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
const { initTooltips } = await import('../../../js/ui/tip/controlTooltip.js');
const { TIP_SHOW_DELAY_MS } = await import('../../../js/ui/motion.js');
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

// The wired bubble at one state of the size line, the fold and the mode: its size line, and
// whether the badge shows at all.
const bubbleAt = ({ size, text = size, collapsed = true, incognito = false }) => {
    const info = doc.getElementById('image-info');
    Object.assign(info, { textContent: text }).dataset.size = size;
    doc.body.classList.toggle('controls-collapsed', collapsed);
    doc.body.classList.toggle('incognito-mode', incognito);
    for (const o of StubObserver.all) o.fn();
    const [sizeText] = doc.getElementById('hints-popup').children;
    return { line: sizeText.nodeValue, shown: doc.getElementById('hints-btn').style.display !== 'none' };
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
test('the ? hints badge owns its bubble and opts out of the floating tooltip', (t) => {
    const badge = markup.slice(markup.indexOf('id="hints-btn"'));
    const openTag = badge.slice(0, badge.indexOf('>'));
    assert.ok(/data-no-tooltip/.test(openTag), '#hints-btn opts out of the shared tooltip');
    assert.ok(!/\stitle=/.test(openTag) && !/data-title=/.test(openTag),
        'no second copy of the shortcut text on the badge itself');
    bubbleRig();
    const hintsBtn = doc.getElementById('hints-btn');
    for (let i = 0; i < 2; i++) doc.getElementById('toggle-controls').dispatch('click');
    assert.ok(!hintsBtn.title && !hintsBtn.hasAttribute('title') && !hintsBtn.dataset.title,
        'and the toggle handler must not put one back');
    t.mock.timers.enable({ apis: ['setTimeout'] });
    initTooltips();
    const tipAfterHover = (optOut) => {
        const el = createStubElement('span', { isConnected: true });
        el.dataset.title = 'Image Size: 800 × 600 px';
        el.closest = (sel) => (sel === '[data-no-tooltip]' && !optOut ? null : el);
        doc.dispatch('pointerover', { target: el, clientX: 4, clientY: 4 });
        t.mock.timers.tick(TIP_SHOW_DELAY_MS);
        return doc.body.children.some((c) => c.id === 'app-tooltip' && c.classList.contains('visible'));
    };
    assert.equal(tipAfterHover(true), false, 'controlTooltip honours the opt-out');
    assert.equal(tipAfterHover(false), true, 'where a plain control raises its tip');
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
    // The old shortcut wall is gone; those hints live in the ℹ info modal (infoConfig.json).
    assert.match(markup, /<span class="hints-popup" id="hints-popup"><\/span>/, 'the bubble ships empty');
    const info = readFileSync(new URL('../../../../common/config/infoConfig.json', import.meta.url), 'utf8');
    for (const hint of ['Ctrl + wheel', 'Alt + wheel', 'Ctrl + Shift + wheel'])
        assert.ok(info.includes(hint), `${hint} must still be documented in the info modal`);
    // Two facts: the #image-info size line, plus an incognito line off body.incognito-mode.
    bubbleRig();
    const popup = doc.getElementById('hints-popup');
    assert.deepStrictEqual(popup.children.map((c) => c.nodeType === 3 ? '#text' : c.className),
        ['#text', 'hints-incognito'], 'the size line and the incognito line, nothing else');
    const SIZE = 'Image Size: 800 × 600 px';
    // Shown when the facts mean something — an image is open (read off the line's own size,
    // never its incognito tag as well)…
    assert.deepStrictEqual(bubbleAt({ size: SIZE, text: `${SIZE}Incognito — not saved` }), { line: SIZE, shown: true });
    // …or while incognito is on, image or not, and only while the toolbar is collapsed: the info
    // line under the open toolbar carries the same two facts.
    assert.deepStrictEqual(bubbleAt({ size: 'No image', incognito: true }), { line: 'No image loaded', shown: true });
    assert.deepStrictEqual(bubbleAt({ size: 'No image' }), { line: '', shown: false });
    assert.deepStrictEqual(bubbleAt({ size: SIZE, incognito: true, collapsed: false }), { line: '', shown: false });
    const css = LAYOUT_CSS;
    assert.match(css, /body\.controls-collapsed \.info \{ height: 0;[^}]*visibility: hidden; \}/,
      'the size line folds away with the rows it belongs to');
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
