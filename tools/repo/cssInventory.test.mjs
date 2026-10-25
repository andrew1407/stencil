// The CSS inventory scanner (cssInventory.mjs) the browser and extension stylesheet pins share.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { scan } from './cssInventory.mjs';

test('css scanner: comments, strings and url() never look like syntax', () => {
  const { rules } = scan(`/* a { color: red } */
    .a::after { content: "}"; background: url(data:image/svg+xml;base64,AA/*x*/BB) no-repeat; }
    .b { content: '\\;'; color: red }`);
  assert.deepEqual(Object.keys(rules).sort(), ['|.a::after', '|.b']);
  assert.equal(rules['|.a::after'].background, 'url(data:image/svg+xml;base64,AA/*x*/BB) no-repeat');
  assert.equal(rules['|.b'].content, "'\\;'");
});

test('css scanner: selector lists sort, whitespace collapses, repeats merge last-wins', () => {
  const { rules, order } = scan('.b ,\n.a { color: red; color: blue }  .a,.b { margin:0 }');
  assert.deepEqual(rules, { '|.a, .b': { color: 'blue', margin: '0' } });
  assert.deepEqual(order, ['|.a, .b', '|.a, .b']);
});

test('css scanner: at-rules key by prelude and nest', () => {
  const { rules } = scan(`@media (min-width: 40rem) { @supports (display: grid) { .a { display: grid } } }
    @keyframes spin { from { rotate: 0deg } to { rotate: 360deg } }
    @property --x { syntax: '<length>'; inherits: false }`);
  assert.deepEqual(Object.keys(rules).sort(), ['@keyframes spin|from', '@keyframes spin|to',
    '@media (min-width: 40rem) @supports (display: grid)|.a', '|@property --x']);
  assert.equal(rules['|@property --x'].syntax, "'<length>'");
});
