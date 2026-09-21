// Static toolbar markup: the Draw pair, the shipped defaults, the captioned Line/Point
// sections and the select option sets. Split from ui-markup.test.js.
import { test } from 'node:test';
import assert from 'node:assert';

import { layout } from '../../js/ui/layout.js';

const markup = layout();
const count = (needle) => markup.split(needle).length - 1;

// The Draw group is two toggles that relabel in place (Start↔Stop, Line↔Rect): both ship the
// width-pinning class, or the row reflows, and Start/Stop is ONE control.
test('draw toggles are single, width-pinned controls', () => {
    assert.ok(!markup.includes('id="start-drawing"'), 'Start/Stop merged into one button');
    assert.ok(!markup.includes('id="stop-drawing"'), 'Start/Stop merged into one button');
    for (const id of ['draw-toggle', 'draw-mode-toggle']) {
        const tag = markup.match(new RegExp(`<button id="${id}"[^>]*>`));
        assert.ok(tag, `${id} present`);
        assert.ok(tag[0].includes('btn-draw-fixed'), `${id} must be width-pinned`);
    }
});

test('disabled defaults preserved', () => {
    assert.ok(markup.includes('id="undo" disabled'), 'undo disabled');
    assert.ok(markup.includes('id="redo" disabled'), 'redo disabled');
    assert.ok(markup.includes('id="chat-send" disabled'), 'chat send disabled until text is typed');
});

test('value defaults preserved', () => {
    assert.ok(markup.includes('id="filter-color" value="#7c3aed"'), 'filterColor default');
    assert.ok(markup.includes('id="line-color" value="#FFFF00"'), 'lineColor default');
    assert.ok(markup.includes('id="line-thickness" value="2"'), 'lineThickness default');
    assert.ok(markup.includes('id="point-size" value="4"'), 'pointSize default');
    assert.ok(markup.includes('id="zoom-input" value="100"'), 'zoomInput default');
    assert.ok(markup.includes('id="custom-page-width" value="21"'), 'customPageWidth default');
    assert.ok(markup.includes('id="custom-page-height" value="29.7"'), 'customPageHeight default');
});

test('every line/point style control carries a visible caption', () => {
    // The two swatches share a default yellow and the number fields are bare digits, so each
    // needs a for=-bound label; the captions mirror the desktop toolbar's.
    const controls = [
        ['line-color', 'Color', 'color'],
        ['point-color', 'Color', 'color'],
        ['line-thickness', 'Thickness', 'number'],
        ['point-size', 'Size', 'number'],
    ];
    for (const [id, caption, type] of controls) {
        const label = new RegExp(`<label for="${id}"[^>]*>${caption}</label>\\s*<input type="${type}" id="${id}"`);
        assert.match(markup, label, `${id} is preceded by its "${caption}" caption`);
    }
});

test('line and point styling are two separate captioned sections', () => {
    // The section split is the contract, not decoration: the bare "Color" captions disambiguate
    // only because Line owns colour/thickness/dash and Point owns its colour and size.
    const section = (label) => {
        const at = markup.indexOf(`<div class="ctrl-section-label">${label}</div>`);
        assert.notStrictEqual(at, -1, `a "${label}" section exists`);
        return markup.slice(at, markup.indexOf('</div>', markup.indexOf('</div>', at) + 1));
    };
    const line = section('Line');
    const point = section('Point');
    for (const id of ['line-color', 'line-thickness', 'line-style']) {
        assert.ok(line.includes(`id="${id}"`), `${id} lives in the Line section`);
        assert.ok(!point.includes(`id="${id}"`), `${id} is not in the Point section`);
    }
    for (const id of ['point-color', 'point-size']) {
        assert.ok(point.includes(`id="${id}"`), `${id} lives in the Point section`);
        assert.ok(!line.includes(`id="${id}"`), `${id} is not in the Line section`);
    }
});

// The toolbar's clusters, in the one order both surfaces build them (desktop:
// MainWindowToolbar.cpp).
test('the sections are the same set, in the same order, as the desktop toolbar rows', () => {
    const order = ['Image', 'Description &amp; attributes', 'Projects', 'Connections &amp; chat',
                   'Edit', 'Line', 'Point', 'Draw', 'View', 'Zoom', 'Page', 'Formula', 'Data',
                   'Settings'];
    const at = order.map((label) => {
        const i = markup.indexOf(`<div class="ctrl-section-label">${label}</div>`);
        assert.notStrictEqual(i, -1, `a "${label}" section exists`);
        return i;
    });
    for (let i = 1; i < at.length; i++)
        assert.ok(at[i] > at[i - 1], `${order[i]} comes after ${order[i - 1]}`);
    // …and f(x,y) belongs to FORMULA, not to PAGE: inside Page, every toggle of the pill
    // resized that section and re-flowed the whole wrapping row around it.
    const formula = markup.slice(at[11], at[12]);
    const page = markup.slice(at[10], at[11]);
    for (const id of ['allow-formulas', 'formula-inputs', 'formula-x', 'formula-y', 'formula-error'])
        assert.ok(formula.includes(`id="${id}"`) && !page.includes(`id="${id}"`),
                  `${id} lives in the Formula section`);
    for (const id of ['page-size', 'unit-select', 'custom-page-width'])
        assert.ok(page.includes(`id="${id}"`), `${id} stays in Page`);
});

test('filter selects offer every mode (toolbar options + ctx-menu radios)', () => {
    // Scope option checks to the #image-filter select itself — the page-size select
    // also has an <option value="custom">, which would otherwise mask a missing Tint.
    const filterSel = markup.slice(markup.indexOf('id="image-filter"'));
    const filterOptions = filterSel.slice(0, filterSel.indexOf('</select>'));
    for (const v of ['none', 'bw', 'sepia', 'invert', 'contour', 'custom']) {
        assert.ok(filterOptions.includes(`<option value="${v}"`), `filter option ${v} present`);
        assert.ok(markup.includes(`name="ctxFilter" value="${v}"`), `ctxFilter radio ${v} present`);
    }
    assert.ok(filterOptions.includes('<option value="custom">Tint</option>'), 'Tint option present');
    assert.ok(markup.includes('value="invert"> Invert'), 'Invert radio label');
    assert.ok(markup.includes('value="contour"> Contour'), 'Contour radio label');
});

test('page-size select: Custom first, then the ISO formats labelled with sizes', () => {
    const sel = markup.slice(markup.indexOf('id="page-size"'));
    const custom = sel.indexOf('<option value="custom">Custom</option>');
    assert.ok(custom !== -1, 'Custom option present');
    assert.ok(custom < sel.indexOf('<option value="A0">'), 'Custom before the named formats');
    // Spot-check the "<name> (<w> × <h>)" labels (no unit word — #unit-select says it once
    // for the row; trailing zeros trimmed; values from PAGE_SIZES in constants.json).
    assert.ok(markup.includes('<option value="A4">A4 (21 × 29.7)</option>'), 'A4 label');
    assert.ok(markup.includes('<option value="B5">B5 (17.6 × 25)</option>'), 'B5 label');
    assert.ok(markup.includes('<option value="C10">C10 (2.8 × 4)</option>'), 'C10 label');
});

test('HTML entities preserved (not decoded)', () => {
    assert.ok(markup.includes('B&amp;W'), 'B&amp;W entity preserved');
    assert.ok(markup.includes('Black &amp; White'), 'Black &amp; White entity preserved');
    assert.ok(markup.includes('Controls &amp; Shortcuts'), 'Controls &amp; Shortcuts entity preserved');
});

test('zoomInput appears exactly once in static markup', () => {
    assert.strictEqual(count('id="zoom-input"'), 1);
});

test('runtime-only ids are NOT in static markup', () => {
    assert.strictEqual(count('fs-clone-'), 0, 'no fs-clone-* in static markup');
    assert.strictEqual(count('fs-coord-panel-clone'), 0, 'no fs-coordPanel-clone in static markup');
    assert.strictEqual(count('image-missing-banner'), 0, 'no imageMissingBanner in static markup');
});

test('no stray backtick or template interpolation leaked into markup', () => {
    assert.strictEqual(count('`'), 0, 'no backticks in markup');
    assert.strictEqual(count('${'), 0, 'no ${ in markup');
});
