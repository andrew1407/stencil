// §13 registry pins for js/llm/opPlan.js: the op names, their flags, one key phrase per
// bullet, capability truth, the prompt censor and the forbidden-name boundary.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  LLM_SYSTEM_PROMPT, EDITOR_SETTINGS_PROMPT, OPS, FORBIDDEN_OPS, ForbiddenOpError,
  assemblePrompts, BROWSER_CAPABILITIES, parseOpPlan, executeOpPlan,
} from '../js/llm/opPlan.js';
import { plan, makeStub } from './helpers/opPlanRig.js';

// §13 registry pins: op names, flags and one key phrase per bullet, never block bytes — the
// prompts are ASSEMBLED from the registry; only the prose core stays byte-pinned above.
const flat = (s) => String(s || '').replace(/\s+/g, ' ');

test('§13: the registered op names are exactly the contract browser surface set, in prompt order', () => {
  assert.deepStrictEqual(Object.keys(OPS), [
    // §2 core ops + §2.1 multi-image, in "Available ops" bullet order
    'crop', 'rotate', 'filter', 'layout', 'formula', 'page', 'blank',
    'undo', 'redo', 'frame', 'image', 'save',
    // §10 editor-settings profile, in settings-block bullet order
    'theme', 'accent', 'lineStyle', 'units', 'view', 'clear', 'openUrl',
    'connect', 'disconnect', 'copy', 'removeProject', 'clearProjects',
    'compare', 'zoom', 'renameProject', 'projectColor', 'blankColor',
    'openProject', 'incognito', 'voiceChat', 'chatPanel', 'dialog', 'clearChat',
  ]);
});

test('§13: each op carries its contract flags', () => {
  const editorSetting = ['theme', 'accent', 'lineStyle', 'units', 'view', 'clear', 'openUrl',
    'connect', 'disconnect', 'copy', 'removeProject', 'clearProjects', 'compare', 'zoom',
    'renameProject', 'projectColor', 'blankColor', 'openProject', 'incognito', 'voiceChat',
    'chatPanel', 'dialog', 'clearChat'];
  const topLevelOnly = ['undo', 'redo', 'image', 'save'];
  const newFrame = ['blank', 'undo', 'redo', 'frame', 'image', 'clear', 'openUrl', 'openProject'];
  const deferred = ['dialog', 'clearChat'];   // §10: executor-deferred to the plan's end
  for (const [name, def] of Object.entries(OPS)) {
    assert.equal(!!def.editorSetting, editorSetting.includes(name), `${name}.editorSetting`);
    assert.equal(!!def.topLevelOnly, topLevelOnly.includes(name), `${name}.topLevelOnly`);
    assert.equal(!!def.newFrame, newFrame.includes(name), `${name}.newFrame`);
    assert.equal(!!def.deferred, deferred.includes(name), `${name}.deferred`);
  }
});

test('§13: every bullet keeps its key semantic phrase', () => {
  const phrases = {
    crop: 'NEVER derive ratio tokens yourself',
    rotate: 'quarter turns only',
    filter: '"custom" is a duotone tint and requires "tint"',
    layout: 'An empty "lines" array REMOVES every drawn line',
    formula: '{"op":"formula","enabled":false} switches formulas OFF entirely',
    page: '{"op":"page","width":20,"height":30} in centimetres (one form or the other)',
    blank: 'centimetre dims ride as "width"/"height" instead of "format"',
    undo: '{"op":"undo","steps":1} / {"op":"redo","steps":1}',
    frame: 'only valid when the current input is a video',
    image: 'giving each image its OWN actions',
    save: 'before switching to the next',
    theme: '"mode" takes no other value',
    accent: 'translate colour names yourself',
    lineStyle: 'change the DEFAULT style for new lines',
    units: 'display units',
    view: 'show or hide points and lines',
    clear: 'REMOVE the working image and its lines',
    openUrl: 'never a second tab or window',
    connect: 'never invent or suggest a new address',
    copy: 'never answer that it cannot be done',
    removeProject: 'confirm before anything is deleted',
    clearProjects: 'you must never clear everything and try to save it back instead',
    compare: 'the exported image is unchanged',
    zoom: 'This never changes the picture — cropping is the crop op',
    renameProject: 'rename the active saved project',
    projectColor: 'the project\'s name colour',
    blankColor: 'KEEPING the drawn lines',
    openProject: 'unsaved work would be replaced',
    incognito: 'only togglable on a blank editor',
    voiceChat: 'turn the hands-free voice chat mode off',
    chatPanel: 'a "dock" on its own opens the panel where it lands',
    dialog: 'when they ask for a change you can make yourself, make it instead',
    clearChat: 'the clear happens after this plan\'s other actions finish',
  };
  // redo and disconnect are documented on their sibling's shared bullet.
  for (const name of ['redo', 'disconnect']) assert.equal(OPS[name].bullet, undefined, name);
  for (const [name, def] of Object.entries(OPS)) {
    if (name === 'redo' || name === 'disconnect') continue;
    assert.ok(def.bullet, `${name} must carry a prompt bullet`);
    assert.ok(flat(def.bullet).includes(phrases[name]), `${name} bullet keeps "${phrases[name]}"`);
  }
  // §10 "also accepts" lines belong to their op's entry, in their block-end order.
  const also = {
    removeProject: 'remove the project that is open right now',
    copy: 'the layout JSON instead of the image',
    accent: 'a named preset persists and syncs',
    lineStyle: 'the defaults of NEW lines',
    openProject: 'the project edited most recently',
  };
  for (const [name, phrase] of Object.entries(also)) {
    assert.ok(flat(OPS[name].also).includes(phrase), `${name} also-line keeps "${phrase}"`);
  }
  assert.deepStrictEqual(Object.keys(OPS).filter((n) => OPS[n].also).sort(), Object.keys(also).sort());
});

// The DEFERRED ops run in their own executor pass at the turn's end (chatRespond
// flushDeferred), and that pass needs its own capability bag or the op dies on the replay.
test('every deferred op\'s capability rides the end-of-turn replay', async () => {
  const { readFileSync } = await import('node:fs');
  const src = readFileSync(new URL('../js/llm/chatRespond.js', import.meta.url), 'utf8');
  const flush = src.slice(src.indexOf('const flushDeferred'), src.indexOf('// One model round'));
  // …and the dedup keeps the LAST of each op: "close this window and open that one" is
  // two dialog actions, and first-wins left the user with neither.
  assert.ok(flush.includes('for (const a of deferred) byOp.set(a.op, a);'), 'last-wins dedup');
  for (const [name, def] of Object.entries(OPS)) {
    if (!def.deferred) continue;
    for (const cap of def.requires || []) {
      assert.ok(flush.includes(cap), `deferred op "${name}" needs ${cap} in the replay bag`);
    }
  }
});

test('§13 capability truth: an op whose capability is not wired drops out of the prompt', () => {
  // The browser wires everything, so the shipped constants carry every bullet…
  const full = assemblePrompts();
  assert.strictEqual(full.systemPrompt, LLM_SYSTEM_PROMPT);
  assert.strictEqual(full.settingsPrompt, EDITOR_SETTINGS_PROMPT);
  // …and assembling with a reduced set drops exactly the unwired op's bullet.
  const without = (cap) => assemblePrompts(new Set([...BROWSER_CAPABILITIES].filter((c) => c !== cap)));
  const noAttach = without('loadAttachment');
  assert.ok(!noAttach.systemPrompt.includes('{"op":"image"'), 'the image bullet drops');
  assert.ok(noAttach.systemPrompt.includes('{"op":"save"'), 'the other bullets stay');
  assert.strictEqual(noAttach.settingsPrompt, EDITOR_SETTINGS_PROMPT);
  // A settings capability takes the op's main bullet AND its also-accepts line with it.
  const noRemove = without('removeProjectNamed');
  assert.ok(!noRemove.settingsPrompt.includes('removeProject'));
  assert.ok(noRemove.settingsPrompt.includes('{"op":"clearProjects"}'));
  assert.strictEqual(noRemove.systemPrompt, LLM_SYSTEM_PROMPT);
  // A chat surface that cannot clear its conversation must not promise it.
  const noClearChat = without('clearChatConversation');
  assert.ok(!noClearChat.settingsPrompt.includes('clearChat'));
  assert.strictEqual(noClearChat.systemPrompt, LLM_SYSTEM_PROMPT);
});

test('§13 censor: a bullet that smells like secret plumbing fails assembly loudly', () => {
  for (const poison of ['set the api key', 'send a Bearer header', 'the auth token to use',
    'change the base url', 'point at another endpoint']) {
    OPS.poisoned = { bullet: `- {"op":"poisoned"} — ${poison}.`, validate() {}, run() {} };
    try {
      assert.throws(() => assemblePrompts(), /Prompt censor/, poison);
    } finally { delete OPS.poisoned; }
  }
  // The legit registry passes: crop's "crop tokens" prose is grammar, not a secret.
  assert.strictEqual(assemblePrompts().systemPrompt, LLM_SYSTEM_PROMPT);
});

test('§13: no registry op uses a forbidden name; the list carries the contract categories', () => {
  for (const name of Object.keys(OPS)) assert.ok(!FORBIDDEN_OPS.has(name), `"${name}" must not be forbidden`);
  for (const name of ['llm', 'provider', 'apiKey', 'paste', 'hotkey', 'shortcut', 'quit', 'exit', 'chat', 'shareTabs'])
    assert.ok(FORBIDDEN_OPS.has(name), `"${name}" must be forbidden`);
});

test('§13: the executor rejects a forbidden op with a typed error even if it parsed', async () => {
  // Through the parser a forbidden name is an unknown op — dropped, never run.
  const parsed = parseOpPlan(plan({ actions: [{ op: 'paste' }] }));
  assert.deepStrictEqual(parsed.actions, []);
  // A plan smuggled straight to the executor is refused before any dispatch.
  const { stub, calls } = makeStub();
  await assert.rejects(
    () => executeOpPlan({ actions: [{ op: 'paste' }], variants: [], warnings: [] }, stub, {}),
    (err) => err instanceof ForbiddenOpError && err.op === 'paste' && /never model-drivable/.test(err.message));
  assert.deepStrictEqual(calls, []);
  // …and inside a variant's actions too.
  await assert.rejects(
    () => executeOpPlan({ actions: [], variants: [{ label: 'v', actions: [{ op: 'llm' }] }], warnings: [] },
      stub, { exportImage: async () => 'x' }),
    (err) => err instanceof ForbiddenOpError && err.op === 'llm');
});
