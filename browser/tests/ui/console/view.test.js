// The scrollback view (js/ui/console/view.js): text only, a class per kind, the help's command
// gutter, the sweep cutting a print's rows together per jump and settling them whole, the clear.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, createStubDocument } from '../../helpers/dom.js';
import { installMemoryStorage } from '../../helpers/memoryStorage.js';
installMemoryStorage();
const { createConsoleView } = await import('../../../js/ui/console/view.js');
const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');

const pre = () => {
  const doc = createStubDocument();
  const el = createStubElement('pre', { ownerDocument: doc, scrollHeight: 100 });
  return el;
};
const timers = [];
const schedule = (fn, ms) => { timers.push([fn, ms]); return timers.length; };
const runTimers = () => { while (timers.length) timers.shift()[0](); };

test('lines are spans by kind; a help row carries its command in bold; nothing is markup', () => {
  setMotionPrefs({ mode: 'none' });
  const el = pre();
  const view = createConsoleView(el, { now: () => 0, schedule });
  view.print(['/status'], 'echo');
  view.print(['<b>x</b>'], 'error');
  view.print([{ kind: 'section', text: 'Image' }, { kind: 'row', text: '/crop <spec>           crop it', head: '/crop <spec>', help: 'crop it' }], 'help');
  assert.deepEqual(view.lines(), ['/status', '<b>x</b>', 'Image', '/crop <spec>           crop it']);
  assert.ok(el.children[0].className.includes('console-line-echo'));
  assert.ok(el.children[1].className.includes('console-line-error'));
  assert.equal(el.children[1].textContent, '<b>x</b>', 'escaped as text, never parsed');
  assert.ok(el.children[2].className.includes('console-line-section'));
  assert.equal(el.children[3].children[0].textContent, '/crop <spec>');
  view.clear();
  assert.deepEqual(view.lines(), []);
  setMotionPrefs({ mode: 'particles' });
});

test('a print is swept in row by row, top to bottom, each jump by jump; an echo is never swept', () => {
  setMotionPrefs({ mode: 'particles', consoleSpeed: 0.5 });
  const el = pre();
  let plans = 0;
  const view = createConsoleView(el, { now: () => 0, schedule, reveal: { plan: () => { plans += 1; return { jumps: 4, stepMs: 13 }; }, reset() {} } });
  view.print(['abcdefgh', 'xy'], 'plain');
  assert.equal(el.children[0].textContent, '', 'blank until the first jump');
  assert.equal(view.revealing(), true);
  assert.deepEqual(view.lines(), ['abcdefgh', 'xy'], 'the full text is known all along');
  timers.shift()[0]();
  assert.equal(el.children[0].textContent, 'ab');
  assert.equal(el.children[1].textContent, '', 'the next row waits for the first to settle, as the Zig console prints a block line by line');
  runTimers();
  assert.equal(el.children[0].textContent, 'abcdefgh');
  assert.equal(el.children[1].textContent, 'xy');
  assert.equal(view.revealing(), false);
  assert.equal(plans, 2, 'every row is its own sweep against the burst tracker');
  view.print(['/x'], 'echo');
  assert.equal(el.children[2].textContent, '/x');
});

test('help rows sweep in too, the bold command head first, and settle with their structure', () => {
  setMotionPrefs({ mode: 'particles', consoleSpeed: 0.5 });
  const el = pre();
  const view = createConsoleView(el, { now: () => 0, schedule, reveal: { plan: () => ({ jumps: 4, stepMs: 13 }), reset() {} } });
  view.print([{ kind: 'row', text: '/crop <spec>    crop it', head: '/crop <spec>', help: 'crop it' }], 'help');
  const row = el.children[0];
  assert.equal(row.children[0].textContent, '', 'blank until the first jump');
  timers.shift()[0]();
  assert.equal(row.children[0].textContent, '/crop ', 'the head types first');
  assert.equal(row.children[1].textContent, '');
  runTimers();
  assert.equal(row.children[0].textContent, '/crop <spec>');
  assert.equal(row.children[1].textContent, '    crop it');
  assert.equal(row.children[0].className, 'console-line-cmd', 'the head keeps its bold');
});

test('a decorator fills the accent runs — the help heads, the echo, the sections — as they type in, and every run can be re-dressed later', () => {
  setMotionPrefs({ mode: 'particles', consoleSpeed: 0.5 });
  const el = pre();
  el.querySelectorAll = (sel) => el.children.flatMap((line) => [line, ...line.children]).filter((n) => sel.split(', ').some((c) => n.className?.split(' ').includes(c.slice(1))));
  const view = createConsoleView(el, { now: () => 0, schedule, reveal: { plan: () => ({ jumps: 2, stepMs: 13 }), reset() {} } });
  const seen = [];
  view.setDecorator((run, shown, full) => { seen.push([shown, full]); run.textContent = shown.toUpperCase(); });
  view.print([{ kind: 'section', text: 'Edit' }, { kind: 'row', text: '/crop <spec>  crop it', head: '/crop <spec>', help: 'crop it' }], 'help');
  view.print([{ text: '> /x', head: '> /x' }], 'echo');
  view.print(['error: no'], 'error');
  runTimers();
  assert.equal(el.children[0].textContent, 'EDIT');
  assert.equal(el.children[1].children[0].textContent, '/CROP <SPEC>');
  assert.equal(el.children[2].children[0].textContent, '> /X');
  assert.equal(el.children[3].children[0].textContent, 'error: ', 'the error prefix is red, never the accent: no decorator');
  assert.ok(seen.some(([shown, full]) => shown.length < full.length && full === '/crop <spec>  crop it'), 'the head is decorated at every cut of the sweep');
  assert.deepEqual(view.runs().map((r) => r.dataset.shown), ['Edit', '/crop <spec>', '> /x'], 'every run knows what it shows, for a re-dress');
  view.setDecorator(null);
  setMotionPrefs({ mode: 'particles' });
});
