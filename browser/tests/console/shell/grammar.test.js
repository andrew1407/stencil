// The console line grammar (js/console/shell/grammar.js), the twin of cli/src/console/commands.zig:
// the slash is optional, the first word is the command, the rest its argument.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseCommandLine, slashed, parseConnectArgs, parseCropSpec, looksLikeServerUrl } from '../../../js/console/shell/grammar.js';

test('a line splits into its word and argument, with or without the slash', () => {
  assert.deepEqual(parseCommandLine('/upload x.png'), { word: 'upload', arg: 'x.png', slashed: true });
  assert.deepEqual(parseCommandLine('upload   x.png  '), { word: 'upload', arg: 'x.png', slashed: false });
  assert.deepEqual(parseCommandLine('/ crop x1=10% x2=90%'), { word: 'crop', arg: 'x1=10% x2=90%', slashed: true });
  assert.deepEqual(parseCommandLine('   '), { word: '', arg: '', slashed: false });
  assert.deepEqual(parseCommandLine('/apply http://h/l.json'), { word: 'apply', arg: 'http://h/l.json', slashed: true });
  assert.equal(slashed(' /meow'), true);
  assert.equal(slashed('meow'), false);
});

test('connect arguments pair each URL with the token word that follows it', () => {
  assert.deepEqual(parseConnectArgs('http://a:8090 tok1 http://b'), [{ url: 'http://a:8090', token: 'tok1' }, { url: 'http://b', token: null }]);
  assert.deepEqual(parseConnectArgs('localhost'), [{ url: 'localhost', token: null }]);
  assert.deepEqual(parseConnectArgs(''), []);
  assert.equal(looksLikeServerUrl('host.example'), true);
  assert.equal(looksLikeServerUrl('abc123'), false);
});

test('a crop spec is edge=value tokens plus album; anything else is no spec', () => {
  assert.deepEqual(parseCropSpec('x1=10% x2=90% album'), { x1: '10%', x2: '90%', album: true });
  assert.deepEqual(parseCropSpec('Y1=2cm aspect=4:3'), { y1: '2cm', aspect: '4:3' });
  assert.equal(parseCropSpec('x1=10% nonsense'), null);
  assert.deepEqual(parseCropSpec(''), {});
});
