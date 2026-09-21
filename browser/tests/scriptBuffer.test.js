// The ONE .stc buffer behind both editors. The script window and the context-menu flyout
// are two views of the same text: typing in either is what the other shows when it opens,
// and neither closing throws the work away. It is session memory only — never persisted.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';

import { scriptText, setScriptText } from '../js/ui/script/scriptBuffer.js';
import { wireScriptEditor } from '../js/ui/script/scriptEditor.js';
import { createStubElement, installDom } from './helpers/dom.js';

// ui/ is split into feature folders, so a bare module name is looked up, not assumed flat.
const UI_DIR = new URL('../js/ui/', import.meta.url);
const uiPath = (n) => {
  const walk = (d) => readdirSync(d, { withFileTypes: true }).flatMap((e) =>
    e.isDirectory() ? walk(new URL(`${e.name}/`, d)) : (e.name === n ? [new URL(e.name, d)] : []));
  return n.includes('/') ? new URL(n, UI_DIR) : walk(UI_DIR)[0];
};
const src = (name) => readFileSync(uiPath(name), 'utf8');

const asPre = (el) => {
  return el;
};

// One editor's worth of stubs under a shared document, wired the way both hosts wire it.
const view = (doc, prefix) => {
  const mk = (suffix, tag = 'div') => doc.register(`${prefix}${suffix}`, createStubElement(tag));
  const editor = mk('editor', 'textarea');
  const pre = asPre(mk('highlight', 'pre'));
  const strip = mk('diag');
  const ids = {
    run: `${prefix}run`, copy: `${prefix}copy`, download: `${prefix}download`,
    clear: `${prefix}clear`, uploadBtn: `${prefix}upload-btn`, upload: `${prefix}upload`,
  };
  for (const id of Object.values(ids)) doc.register(id, createStubElement('button'));
  const { dispose } = wireScriptEditor({
    editor, pre, strip, ids, app: { export: { downloadBlob: () => {} } },
    onRun: async () => {}, onUpload: () => {},
  });
  const type = (text) => { editor.value = text; editor.dispatch('input'); };
  return { editor, strip, ids, type, dispose, painted: () => pre.childNodes.filter((n) => n.className).map((n) => n.className) };
};

const rig = (t) => {
  setScriptText('');
  const doc = installDom({}, { window: {} });
  doc.createTextNode = (text) => ({ nodeType: 3, textContent: text });
  const win = view(doc, 'script-');
  const fly = view(doc, 'ctx-script-');
  t.after(() => { win.dispose(); fly.dispose(); doc.restore(); });
  return { doc, win, fly };
};

test('typing in one editor is what the other is showing', (t) => {
  const { win, fly } = rig(t);
  win.type('@crop 10%');
  assert.equal(scriptText(), '@crop 10%', 'the buffer took it');
  assert.equal(fly.editor.value, '@crop 10%', 'and the other view followed');
  assert.ok(fly.painted().includes('stk-directive'), 'repainted, not just re-valued');

  fly.type('@filter bw');
  assert.equal(win.editor.value, '@filter bw', 'and back the other way');
});

test('an editor wired later opens onto the text that is already there', (t) => {
  const { doc, win } = rig(t);
  win.type('@save');
  const late = view(doc, 'late-');
  t.after(() => late.dispose());
  assert.equal(late.editor.value, '@save', 'it adopts the buffer, never the other textarea');
  assert.equal(doc.getElementById('late-run').disabled, false, 'gated on the text it adopted');
});

test('dropping one view off the buffer leaves the text for the other', (t) => {
  const { win, fly } = rig(t);
  fly.type('@rotate 90');
  fly.dispose();                       // the flyout torn down is the menu closing on it
  assert.equal(scriptText(), '@rotate 90');
  win.type('@rotate 180');
  assert.equal(fly.editor.value, '@rotate 90', 'a dropped view stops being told');
  assert.equal(win.editor.value, '@rotate 180');
});

test('Clear empties the buffer for every view at once, and re-gates them all', async (t) => {
  const { doc, win, fly } = rig(t);
  win.type('@nope 5');
  doc.getElementById('script-run').dispatch('click');
  await new Promise((r) => setTimeout(r, 0));
  assert.notEqual(win.strip.textContent, '', 'a run leaves a verdict on screen');

  doc.getElementById('script-clear').dispatch('click');
  assert.equal(win.strip.textContent, '', 'Clear takes the strip with it');
  assert.ok(!win.painted().some((c) => c.includes('stk-error')), 'and the underlines');
  assert.equal(scriptText(), '');
  assert.equal(win.editor.value, '');
  assert.equal(fly.editor.value, '', 'the flyout empties with it');
  for (const id of ['script-run', 'script-copy', 'script-download', 'script-clear',
    'ctx-script-run', 'ctx-script-copy', 'ctx-script-download', 'ctx-script-clear']) {
    assert.equal(doc.getElementById(id).disabled, true, `${id} is gated again`);
  }
  assert.equal(doc.getElementById('script-upload-btn').disabled, false, 'Upload always works');
});

test('Copy and Download act on the buffer, so they cannot disagree with the screen', () => {
  const wiring = src('scriptEditor.js');
  assert.match(wiring, /navigator\.clipboard\.writeText\(scriptText\(\)\)/);
  assert.match(wiring, /new Blob\(\[scriptText\(\)\]/);
  assert.match(wiring, /paintInto\(pre, scriptText\(\), checked\)/, 'the paint reads it too');
});

test('the script survives closing the window — nothing clears it on the way in or out', () => {
  const modal = src('scriptModal.js');
  const shell = modal.slice(modal.indexOf('shell = wireModalShell'));
  assert.ok(!/editor\.value = ''/.test(shell), 'onOpen/onClose no longer empty the editor');
  assert.match(modal, /Clear is the way out/, 'and the file says so, instead of the opposite');
});

test('the buffer is session memory: no storage, nothing that survives a reload', () => {
  const code = src('scriptBuffer.js');
  assert.doesNotMatch(code, /localStorage|sessionStorage|indexedDB|cookie|fetch\(/,
    'a script is untrusted text — persisting it silently is not wanted');
  assert.match(code, /^let text = '';$/m, 'a fresh module starts empty');
  // Neither host may quietly keep its own copy, or the two could drift.
  for (const name of ['scriptModal.js', 'ctxScriptEditor.js']) {
    assert.doesNotMatch(src(name), /localStorage|sessionStorage/, name);
  }
});
