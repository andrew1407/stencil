import { test } from 'node:test';
import assert from 'node:assert';
import { cmToUnit, unitToCm, unitLabel, defaultUnitFromLocale, CM_PER_INCH } from '../js/utils.js';

test('cmToUnit / unitToCm round-trip', () => {
  assert.equal(cmToUnit(2.54, 'in'), 1);
  assert.equal(cmToUnit(5, 'cm'), 5);
  assert.equal(unitToCm(1, 'in'), CM_PER_INCH);
  assert.equal(unitToCm(5, 'cm'), 5);
});

test('unitLabel', () => {
  assert.equal(unitLabel('in'), 'in');
  assert.equal(unitLabel('cm'), 'cm');
  assert.equal(unitLabel('anything-else'), 'cm');
});

test('defaultUnitFromLocale: explicit imperial regions → inches', () => {
  assert.equal(defaultUnitFromLocale({ language: 'en-US' }), 'in');
  assert.equal(defaultUnitFromLocale({ language: 'en-LR' }), 'in');     // Liberia
  assert.equal(defaultUnitFromLocale({ languages: ['my-MM'] }), 'in');  // Myanmar
});

test('defaultUnitFromLocale: metric regions → cm', () => {
  assert.equal(defaultUnitFromLocale({ language: 'en-GB' }), 'cm');
  assert.equal(defaultUnitFromLocale({ language: 'fr-FR' }), 'cm');
  assert.equal(defaultUnitFromLocale({ language: 'de-DE' }), 'cm');
  assert.equal(defaultUnitFromLocale({ language: 'uk-UA' }), 'cm');
});

test('defaultUnitFromLocale: languages[] takes precedence over language', () => {
  assert.equal(defaultUnitFromLocale({ languages: ['en-US'], language: 'fr-FR' }), 'in');
});

test('defaultUnitFromLocale: bare tag is resolved to its likely region (en → US)', () => {
  assert.equal(defaultUnitFromLocale({ language: 'en' }), 'in');
  assert.equal(defaultUnitFromLocale({ language: 'fr' }), 'cm');
});

test('defaultUnitFromLocale: missing/garbage navigator never throws, falls back to cm', () => {
  // null / {} are the explicit "no locale info" sentinels — they exercise the
  // metric fallback. (Passing `undefined` would instead trigger the default
  // parameter and read the live globalThis.navigator, which is not the path
  // under test here.)
  assert.equal(defaultUnitFromLocale(null), 'cm');
  assert.equal(defaultUnitFromLocale({}), 'cm');
  assert.equal(defaultUnitFromLocale({ language: '' }), 'cm');
  assert.equal(defaultUnitFromLocale({ language: '!!!not-a-tag!!!' }), 'cm');
});
