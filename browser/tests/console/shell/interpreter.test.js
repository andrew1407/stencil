// One line through the interpreter (js/console/shell/interpreter.js): the echo, the unknown-word
// error, a handler's lines and its thrown error, the history and Tab completion.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStencil, makeApp } from '../../helpers/stencilApiRig.js';
import { createInterpreter } from '../../../js/console/shell/interpreter.js';

const rig = ({ cliMode = false } = {}) => {
  const app = makeApp();
  const printed = [];
  const secrets = [];
  const echoes = [];
  let statusReads = 0;
  const host = {
    statusChanged: () => { statusReads += 1; },
    print: (lines, kind) => {
      printed.push([kind, ...lines.map((l) => (typeof l === 'string' ? l : l.text))]);
      if (kind === 'echo') echoes.push({ head: lines[0].head });
    },
    clear: () => printed.push(['clear']), close: () => printed.push(['close']), pickFile() {},
    cliMode: () => cliMode, secret: (w) => { secrets.push(w); return w === 'meow'; }, isSecret: (w) => w === 'meow',
  };
  const it = createInterpreter({ app, host, stencil: createStencil(app), extra: { ll: () => ['row'] } });
  return { it, printed, secrets, echoes, app, statusReads: () => statusReads };
};

test('a line is echoed as the Zig console does, then its handler prints; an unknown word prints the error', async () => {
  const { it, printed, echoes, statusReads } = rig();
  await it.run('/status x1');
  assert.equal(statusReads(), 1, 'every command ends with the header reading the picture again');
  assert.deepEqual(printed[0], ['echo', '> /status x1']);
  assert.deepEqual(echoes[0], { head: '> /status' }, 'the prompt and the command word wear the accent, the arguments stay plain');
  assert.equal(printed[1][0], 'plain');
  await it.run('frobnicate');
  assert.deepEqual(echoes.at(-1), { head: '> ' }, 'an unknown word gets the prompt only');
  assert.deepEqual(printed.at(-1), ['error', "error: unknown command 'frobnicate' — type 'help' for the command list"]);
  await it.run('   ');
  assert.equal(printed.length, 4, 'a blank line prints nothing');
});

test('a thrown handler error prints as one error line, help prints help lines', async () => {
  const { it, printed } = rig();
  await it.run('/rotate 1');
  assert.deepEqual(printed.at(-1), ['error', "error: no image loaded — '/upload', '/blank' or '/open' first"]);
  await it.run('/help');
  assert.equal(printed.at(-1)[0], 'help');
});

test('CLI mode: a slashed secret word is taken before the table, a bare one is not; its rows answer', async () => {
  const { it, printed, secrets } = rig({ cliMode: true });
  await it.run('/meow');
  assert.deepEqual(secrets, ['meow']);
  assert.deepEqual(printed, [['secret', '> /meow']], 'only the echo, whole in gold');
  await it.run('meow');
  assert.equal(printed.at(-1)[0], 'error', 'a bare secret is an unknown command');
  await it.run('/ll');
  assert.deepEqual(printed.at(-1), ['plain', 'row']);
  await rig().it.run('/ll').then(() => {});
});

test('an interpreter wired before the facade exists finds it on the first line', async () => {
  const app = makeApp({ image: { width: 4, height: 2 } });
  const printed = [];
  const had = globalThis.window;
  globalThis.window = {};
  const it = createInterpreter({ app, host: { print: (lines, kind) => printed.push([kind, ...lines]), cliMode: () => false, secret: () => false } });
  globalThis.window.stencil = createStencil(app);
  await it.run('/rotate 1');
  globalThis.window = had;
  assert.deepEqual(app.calls.at(-1), ['rotateImage', 1], 'the facade is the one installed by then');
  assert.ok(!printed.some(([k]) => k === 'error'), printed.map((p) => p.join(' ')).join('\n'));
});

test('the line being typed is classified for the prompt colour: a command, a secret, or nothing', () => {
  const { it } = rig({ cliMode: true });
  assert.equal(it.classify('/stat'), '', 'not yet a command');
  assert.equal(it.classify('/status x1'), 'cmd');
  assert.equal(it.classify('status'), 'cmd', 'slash or not');
  assert.equal(it.classify('/meow'), 'secret');
  assert.equal(it.classify('meow'), '', 'a secret answers only slashed');
  assert.equal(rig().it.classify('/meow'), '', 'and only in CLI mode');
});

test('a secret word that throws prints one error line, like any handler', async () => {
  const { printed } = rig({ cliMode: true });
  const bad = createInterpreter({ app: makeApp(), stencil: createStencil(makeApp()), host: {
    print: (lines, kind) => printed.push([kind, ...lines]), cliMode: () => true, secret: () => { throw new Error('no bulbs'); },
  } });
  await bad.run('/fairylight');
  assert.deepEqual(printed.at(-1), ['error', 'error: no bulbs']);
});

test('history recalls what was typed, a repeat once; Tab completes as the Zig line editor does', async () => {
  const { it, printed } = rig();
  await it.run('/status');
  await it.run('/help');
  await it.run('/help');
  assert.deepEqual(it.history.entries, ['/status', '/help'], 'a consecutive repeat is kept once');
  assert.equal(it.history.up(), '/help');
  assert.equal(it.history.up(), '/status');
  assert.equal(it.history.up(), '/status');
  assert.equal(it.history.down(), '/help');
  assert.equal(it.history.down(), '');
  assert.equal(it.history.atEnd(), true);
  assert.equal(it.complete('/connecti'), '/connections ', 'one match fills in, a space after');
  assert.equal(it.complete('conn'), 'connect', 'several extend to their common prefix, the slash as typed');
  const before = printed.length;
  assert.equal(it.complete('/c'), null, 'an ambiguous one changes nothing…');
  assert.equal(printed.length, before + 1, '…and lists the options');
  assert.match(printed.at(-1)[1], /^\/c\w+(  \/c\w+)+$/);
  assert.ok(printed.at(-1)[1].includes('/connect'));
  assert.equal(it.complete('/status x'), null, 'only the first word completes');
  assert.equal(it.complete('zzz'), null);
});

test('a typed key is echoed masked and recalled bare, as the Zig console does (llm/key.zig)', async () => {
  const { it, printed } = rig();
  await it.run('/llm key abc123');
  const echo = printed.find((p) => p[0] === 'echo');
  assert.equal(echo[1], '> /llm key ••••••••');
  assert.ok(printed.every((p) => !p.join(' ').includes('abc123')), 'the key is nowhere in the scrollback');
  assert.deepEqual(it.history.entries, ['/llm key'], 'recalled, the bare form asks for the key again');
  await it.run('/llm model x');
  assert.deepEqual(it.history.entries, ['/llm key', '/llm model x']);
});
