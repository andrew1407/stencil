// parseLaunchQuery (js/core/launch/query.js) beyond the corpus: the value coercion, the carrier
// rule, the script cap, the secret list, and the shapes a hostile or sloppy URL can take.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  parseLaunchQuery, coerceLaunchValue, isLaunchQueryHash, LAUNCH_QUERY_SECRET_KEYS,
} from '../../../js/core/launch/query.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };

test('coerceLaunchValue: words become booleans, number shapes numbers, the rest stays text', () => {
  for (const w of ['true', 'TRUE', ' yes ', 'on']) assert.equal(coerceLaunchValue(w), true, w);
  for (const w of ['false', 'No', 'off']) assert.equal(coerceLaunchValue(w), false, w);
  assert.equal(coerceLaunchValue('3'), 3);
  assert.equal(coerceLaunchValue('-2.5'), -2.5);
  assert.equal(coerceLaunchValue('0'), 0, 'a zero is a number, not a boolean');
  assert.equal(coerceLaunchValue('1'), 1, 'a one is a number, not a boolean');
  assert.equal(coerceLaunchValue('red'), 'red');
  assert.equal(coerceLaunchValue('#ff0000'), '#ff0000');
  assert.equal(coerceLaunchValue('x*2'), 'x*2', 'a formula stays as typed');
  assert.equal(coerceLaunchValue(''), '', 'an empty value is an empty string');
});

test('isLaunchQueryHash: any #a=b fragment but the JSON hand-offs', () => {
  assert.equal(isLaunchQueryHash('#theme=dark'), true);
  assert.equal(isLaunchQueryHash('#stencil=%7B%7D'), false);
  assert.equal(isLaunchQueryHash('#stencil-desktop=x'), false);
  assert.equal(isLaunchQueryHash('#section'), false, 'no pair, no query');
  assert.equal(isLaunchQueryHash(''), false);
  assert.equal(isLaunchQueryHash(null), false);
});

test('the search is read with or without its leading ?', () => {
  assert.deepEqual(parseLaunchQuery('theme=dark').theme, 'dark');
  assert.deepEqual(parseLaunchQuery('?theme=dark').theme, 'dark');
});

test('the secret keys are exactly the connection token and the assistant key', () => {
  assert.deepEqual([...LAUNCH_QUERY_SECRET_KEYS], ['token', 'llm-api-key']);
  const q = parseLaunchQuery('?llm-key=k&llm-token=t&token=x&connect=https://s.example');
  assert.deepEqual(q.llm, {}, 'no alias of the key gets through the query');
  assert.deepEqual(q.connect, [{ url: 'https://s.example', token: '' }]);
  assert.deepEqual(q.warnings.map((w) => w.code), ['secret-in-query', 'secret-in-query', 'secret-in-query']);
});

test('the script cap is the shared LAUNCH.scriptMaxChars: at the cap it rides, past it is dropped', () => {
  const max = constants.LAUNCH.scriptMaxChars;
  const atCap = parseLaunchQuery(`?script=${'a'.repeat(max)}`);
  assert.equal(atCap.script.length, max);
  assert.deepEqual(atCap.warnings, []);
  const over = parseLaunchQuery(`?script=${'a'.repeat(max + 1)}`);
  assert.equal(over.script, '');
  assert.deepEqual(over.warnings, [{ code: 'bad-value', key: 'script' }]);
  assert.equal(over.opensSession, false, 'a dropped script opens nothing');
});

test('a malformed percent-encoding never throws', () => {
  assert.doesNotThrow(() => parseLaunchQuery('?name=%E0%A4%A&theme=dark'));
  assert.equal(parseLaunchQuery('?name=%E0%A4%A&theme=dark').theme, 'dark');
});

test('settings candidates are plain own keys: no prototype reaches the bag', () => {
  const q = parseLaunchQuery('?__proto__=1&constructor=2&prototype=3&thickness=4&=5');
  assert.equal(Object.getPrototypeOf(q.settings), Object.prototype);
  assert.deepEqual(Object.keys(q.settings).sort(), ['proto', 'thickness']);
  assert.equal(q.settings.thickness, 4);
  assert.equal(Object.hasOwn(q.settings, 'constructor'), false);
});

test('a blank keeps its size only when both sides are given and positive', () => {
  assert.deepEqual(parseLaunchQuery('?blank=red&width=10').picture, { kind: 'blank', color: 'red', width: 10, height: null });
  assert.deepEqual(parseLaunchQuery('?blank=red&width=10&height=0').picture, { kind: 'blank', color: 'red', width: 10, height: null });
  assert.deepEqual(parseLaunchQuery('?blank=').picture, { kind: 'blank', color: '#ffffff', width: null, height: null });
});

test('an empty value drops a text field but turns a flag on', () => {
  const q = parseLaunchQuery('?name=&desc=%20&incognito=&chat=');
  assert.equal(q.name, null);
  assert.equal(q.project.description, null);
  assert.equal(q.incognito, true);
  assert.equal(q.chat.open, true);
});

test('incognito and chat read their words, and anything else is off', () => {
  assert.equal(parseLaunchQuery('?incognito=false').incognito, false);
  assert.equal(parseLaunchQuery('?incognito=0').incognito, false);
  assert.equal(parseLaunchQuery('?incognito=maybe').incognito, false);
  assert.equal(parseLaunchQuery('?chat=closed').chat.open, false);
  assert.equal(parseLaunchQuery('?chat=true').chat.open, true);
});

test('a connect with no url is dropped, and a bare token beside it stays unpaired', () => {
  const q = parseLaunchQuery('', '#connect=&token=t&connect=https://b.example');
  assert.deepEqual(q.connect, [{ url: 'https://b.example', token: '' }]);
});

test('a fragment alone is a launch; a fragment hand-off beside a query is left to applyExternalLaunch', () => {
  const alone = parseLaunchQuery('', '#blank=red');
  assert.equal(alone.picture.kind, 'blank');
  assert.equal(alone.fromHash, true);
  const beside = parseLaunchQuery('?theme=dark', '#stencil=%7B%7D');
  assert.equal(beside.theme, 'dark');
  assert.equal(beside.fromHash, false, 'the JSON fragment is not consumed here');
  assert.equal(beside.picture, null);
});

test('the result is fresh each call and the parser keeps no state', () => {
  const a = parseLaunchQuery('?thickness=1');
  const b = parseLaunchQuery('?thickness=2');
  assert.notEqual(a, b);
  assert.equal(a.settings.thickness, 1);
  assert.equal(b.settings.thickness, 2);
});
