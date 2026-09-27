import { test } from 'node:test';
import assert from 'node:assert';

// The shared verdict vocabulary. Each of these wraps a primitive that already existed —
// the point of the suite is that the SHAPE is one shape, and that a rejection says why.
import {
  invalid, validateProjectName, validateHexColor, validateHttpUrl,
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
    validateHexColor(''), validateHttpUrl('javascript:1'),
    validateProjectName({ validateName: () => invalid('taken') }, 'x'),
  ];
  for (const r of rejections) {
    assert.deepStrictEqual(Object.keys(r).sort(), ['ok', 'reason']);
    rejects(r, 'the sample');
  }
});

test('an accepted verdict is frozen — callers pass it around and must not edit it', () => {
  const ok = validateHttpUrl('https://example.com');
  assert.throws(() => { ok.ok = false; }, TypeError);
  assert.strictEqual(validateHexColor('#abc'), ok, 'every validator hands out the one accepted verdict');
  assert.strictEqual(invalid('why').ok, false);
});

test('a project name asks the STORE, and a store-less caller is not blocked', () => {
  const store = { validateName: (n) => (n === 'taken' ? { ok: false, reason: 'taken' } : { ok: true, reason: '' }) };
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

test('an endpoint is http(s) only', () => {
  accepts(validateHttpUrl('https://example.test/v1'), 'https');
  accepts(validateHttpUrl('http://localhost:11434'), 'plain http (loopback)');
  for (const bad of ['javascript:alert(1)', 'file:///etc/passwd', 'chrome-extension://x', '', null])
    rejects(validateHttpUrl(bad), `"${bad}"`);
});
