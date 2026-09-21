import { test } from 'node:test';
import assert from 'node:assert';

// The shared verdict vocabulary. Each of these wraps a primitive that already existed —
// the point of the suite is that the SHAPE is one shape, and that a rejection says why.
import {
  VALID, invalid, validateProjectName, validateHexColor, validateAccent, validatePageSize,
  validateLengthToken, validateDuration, validateHttpUrl, validateHotkey, validateFormula,
  validateProjectFileText,
} from '../../../js/core/parse/validation.js';

const rejects = (res, what) => {
  assert.strictEqual(res.ok, false, `${what} must be rejected`);
  assert.ok(res.reason && typeof res.reason === 'string', `${what} must say why`);
};
const accepts = (res, what) => {
  assert.strictEqual(res.ok, true, `${what} must be accepted`);
  assert.strictEqual(res.reason, '', `an accepted ${what} carries no reason`);
};

test('every validator answers the SAME shape, and a rejection always carries a sentence', () => {
  const rejections = [
    validateHexColor(''), validateAccent('nope'), validatePageSize('A99'),
    validateLengthToken('sideways'), validateDuration('later'), validateHttpUrl('javascript:1'),
    validateHotkey(''), validateFormula({ validate: () => false }, 'x +', 'x'),
  ];
  for (const r of rejections) {
    assert.deepStrictEqual(Object.keys(r).sort(), ['ok', 'reason']);
    rejects(r, 'the sample');
  }
});

test('VALID is frozen — callers pass the verdict around and must not edit it', () => {
  assert.throws(() => { 'use strict'; VALID.ok = false; }, TypeError);
  assert.strictEqual(invalid('why').ok, false);
});

test('a project name asks the STORE, and a store-less caller is not blocked', () => {
  const store = { validateName: (n) => (n === 'taken' ? { ok: false, reason: 'taken' } : VALID) };
  accepts(validateProjectName(store, 'fresh'), 'a free name');
  rejects(validateProjectName(store, 'taken'), 'a duplicate');
  accepts(validateProjectName(null, 'anything'), 'a name with no store to ask');
});

test("a colour: '' is rejected unless the field means something by it", () => {
  accepts(validateHexColor('#ff5623'), 'a full hex');
  accepts(validateHexColor('  #abc '), 'a short hex, trimmed');
  rejects(validateHexColor('rebeccapurple'), 'a CSS name the picker cannot produce');
  rejects(validateHexColor(''), 'a blank colour by default');
  accepts(validateHexColor('', { allowEmpty: true }), "a cleared colour where '' is meaningful");
  accepts(validateHexColor(null, { allowEmpty: true }), 'a missing colour where blank is allowed');
});

test('an accent is a NAMED accent or any hex the picker could produce', () => {
  accepts(validateAccent('#0a0a0a'), 'a hex accent');
  rejects(validateAccent('chartreuse'), 'a colour that is neither');
  rejects(validateAccent(''), 'nothing at all');
});

test('page sizes and length tokens come off the shared tables', () => {
  accepts(validatePageSize('a4'), 'a page size, case-insensitively');
  accepts(validatePageSize('custom'), 'the custom page');
  rejects(validatePageSize('A99'), 'a size not on the table');
  accepts(validateLengthToken('40'), 'a bare delta');
  accepts(validateLengthToken('2cm'), 'a cm token');
  accepts(validateLengthToken('25%'), 'a percentage');
  rejects(validateLengthToken('sideways'), 'a word');
});

test('a duration accepts keep-forever, and an endpoint is http(s) only', () => {
  accepts(validateDuration('7 days'), 'a week');
  accepts(validateDuration('never'), 'keep forever');
  rejects(validateDuration('soon'), 'a word with no unit');
  accepts(validateHttpUrl('https://example.test/v1'), 'https');
  accepts(validateHttpUrl('http://localhost:11434'), 'plain http (loopback)');
  for (const bad of ['javascript:alert(1)', 'file:///etc/passwd', 'chrome-extension://x', '', null])
    rejects(validateHttpUrl(bad), `"${bad}"`);
});

test('a hotkey goes through the shared parser', () => {
  accepts(validateHotkey('Ctrl+Shift+K'), 'a real combo');
  rejects(validateHotkey(''), 'an empty combo');
});

test('a blank formula is the identity, so it is valid without asking the engine', () => {
  let asked = 0;
  const engine = { validate: () => { asked++; return true; } };
  accepts(validateFormula(engine, '   ', 'x'), 'a blank formula');
  assert.strictEqual(asked, 0, 'the engine is not consulted for a blank');
  accepts(validateFormula(engine, 'x*2', 'x'), 'an expression the engine likes');
  const res = validateFormula({ validate: () => false }, 'x +', 'y');
  rejects(res, 'an unparseable expression');
  assert.match(res.reason, /Invalid y formula/, 'the reason names the axis');
});

test('a .stencil file comes back as a verdict, with the parsed project alongside', () => {
  const bad = validateProjectFileText('{not json');
  rejects(bad, 'junk');
  const alsoBad = validateProjectFileText(JSON.stringify({ format: 'nope' }));
  rejects(alsoBad, 'the wrong format sentinel');
  assert.strictEqual(alsoBad.project, undefined, 'a rejection carries no project');
});
