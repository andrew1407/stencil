// The incognito frame traces the canvas VIEWPORT (js/ui/panel/mainContent.js): rendered inside
// the scrollport ahead of the canvas container, sticky in CSS, and sized from the viewport's
// own box, which the wired panel republishes on resize and never per scroll.
import { test } from 'node:test';
import assert from 'node:assert';
import { layout } from '../../../js/ui/layout.js';
import { StencilMainContent } from '../../../js/ui/panel/mainContent.js';
import { LAYOUT_CSS, COMPONENTS_CSS } from '../../helpers/css.js';
import { createStubElement, installDom } from '../../helpers/dom.js';

const markup = layout();

// The frame belongs to the EDITOR: it traces the whole visible canvas region, picture and
// empty ground, at any zoom — round the image alone it reads as a selection (user report).
test('the incognito frame traces the canvas VIEWPORT, not the picture', (t) => {
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
    const doc = installDom({ autoCreateById: true }, { matchMedia: () => ({ matches: false }) });
    t.after(doc.restore);
    const observers = [];
    globalThis.ResizeObserver = class { constructor(fn) { observers.push(this); this.fn = fn; } observe(el) { this.el = el; } };
    const vp = doc.register('canvas-viewport', createStubElement('div', { clientWidth: 640, clientHeight: 480 }));
    Object.create(StencilMainContent.prototype).wire(null);
    delete globalThis.ResizeObserver;
    assert.deepStrictEqual([vp.style['--vp-w'], vp.style['--vp-h']], ['640px', '480px']);
    Object.assign(vp, { clientWidth: 900, clientHeight: 300 });
    observers.find((o) => o.el === vp).fn();
    assert.deepStrictEqual([vp.style['--vp-w'], vp.style['--vp-h']], ['900px', '300px'], 'a resize republishes');
    assert.strictEqual(vp.listeners.scroll, undefined, 'no per-scroll work');
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
