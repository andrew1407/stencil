// serializeSession (js/core/layout.js): a pure projection of a plain state object, emitting
// every descriptor field in table order. Split from layout.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { serializeSession, LAYOUT_FIELDS } from '../../js/core/layout.js';

const sampleState = () => ({
  imageWidth: 800, imageHeight: 600,
  cropRect: { x: 1, y: 2, width: 3, height: 4 }, rotationQuarters: 0,
  lines: [{ points: [{ x: 1, y: 2 }] }],
  pageSize: 'A3', customPageWidth: 21, customPageHeight: 29.7, unit: 'cm',
  color: '#FFFF00', thickness: 2, pointSize: 4, style: 'solid',
  showPoints: true, showLines: true, imageFilter: 'none', filterColor: '#7c3aed',
  zoom: 1, scrollLeft: 0, scrollTop: 0,
  imageBaseName: 'pic', imageExt: 'png', imageSource: null, imageResource: null,
  tooltipEnabled: true, tooltipShowPage: true, tooltipShowScreen: true, tooltipShowCoords: true,
  allowFormulas: false, formulaX: '', formulaY: '',
  drawMode: 'line', holdDrawDelay: 500,
  selGlowColor: '#ffc800', hoverRingColor: '#7c3aed', focusRingColor: '#7c3aed', defaultFillColor: '#3399ff',
});

test('serializeSession: pure, no DOM/app needed — projects a plain state object', () => {
  const out = serializeSession(sampleState());
  assert.strictEqual(out.imageWidth, 800);
  assert.strictEqual(out.pageSize, 'A3');
  assert.strictEqual(out.defaultFillColor, '#3399ff');
});

test('serializeSession: passes lines through by reference (no copy)', () => {
  const state = sampleState();
  assert.strictEqual(serializeSession(state).lines, state.lines);
});

test('serializeSession: emits every descriptor field in table (byte) order', () => {
  const keys = Object.keys(serializeSession(sampleState()));
  assert.deepStrictEqual(keys, LAYOUT_FIELDS.map(f => f.key));
  // Guard the exact head order that differs from the export subset (crop/rotation before lines).
  assert.deepStrictEqual(keys.slice(0, 5),
    ['imageWidth', 'imageHeight', 'cropRect', 'rotationQuarters', 'lines']);
});

test('serializeSession serializes byte-identically to the old #buildLayout literal', () => {
  const expected =
`{
  "imageWidth": 800,
  "imageHeight": 600,
  "cropRect": {
    "x": 1,
    "y": 2,
    "width": 3,
    "height": 4
  },
  "rotationQuarters": 0,
  "lines": [
    {
      "points": [
        {
          "x": 1,
          "y": 2
        }
      ]
    }
  ],
  "pageSize": "A3",
  "customPageWidth": 21,
  "customPageHeight": 29.7,
  "unit": "cm",
  "color": "#FFFF00",
  "thickness": 2,
  "pointSize": 4,
  "style": "solid",
  "showPoints": true,
  "showLines": true,
  "imageFilter": "none",
  "filterColor": "#7c3aed",
  "zoom": 1,
  "scrollLeft": 0,
  "scrollTop": 0,
  "imageBaseName": "pic",
  "imageExt": "png",
  "imageSource": null,
  "imageResource": null,
  "tooltipEnabled": true,
  "tooltipShowPage": true,
  "tooltipShowScreen": true,
  "tooltipShowCoords": true,
  "allowFormulas": false,
  "formulaX": "",
  "formulaY": "",
  "drawMode": "line",
  "holdDrawDelay": 500,
  "selGlowColor": "#ffc800",
  "hoverRingColor": "#7c3aed",
  "focusRingColor": "#7c3aed",
  "defaultFillColor": "#3399ff"
}`;
  assert.strictEqual(JSON.stringify(serializeSession(sampleState()), null, 2), expected);
});


test('serializeSession emits pointSize and never markerSize', () => {
  const payload = serializeSession(sampleState());
  assert.ok('pointSize' in payload, 'pointSize is written');
  assert.ok(!('markerSize' in payload), 'the old name is never written');
});
