import { test } from 'node:test';
import assert from 'node:assert';

// The chat orchestrator itself (js/llm/chatSession.js): the seams the surface tests
// don't own — the REAL capability closures sharedChatController injects (driven here
// against a recording stub app), the §10 project-name helpers, and the logged-turn
// frame's error / abort / never-left-spinning paths. The shared transcript, the probe
// cache and describeChatError's table are pinned by ctx-assistant.test.js and
// chatTranscriptRender.test.js; nothing here re-tests those.
import {
  uniqueProjectName, resolveProjectByName,
  sharedChatController, forgetChatController,
  chatLog, resetChatLog, chatTurnInFlight,
  runLoggedChatTurn, unreachableText,
} from '../js/llm/chatSession.js';
import { LlmError } from '../js/llm/llmClient.js';

// ── §10 name helpers (pure) ─────────────────────────────────────────────────
test('uniqueProjectName suffixes until the name is free', () => {
  // No store (or one without nameExists) → the wanted name, unchanged.
  assert.strictEqual(uniqueProjectName({}, 'cat'), 'cat');
  assert.strictEqual(uniqueProjectName({ storage: { store: {} } }, 'cat'), 'cat');
  const app = (taken) => ({ storage: { store: { nameExists: (n) => taken.includes(n) } } });
  assert.strictEqual(uniqueProjectName(app([]), 'cat'), 'cat');
  // A batch of saves wanting the same base counts up instead of losing the save.
  assert.strictEqual(uniqueProjectName(app(['cat']), 'cat'), 'cat 2');
  assert.strictEqual(uniqueProjectName(app(['cat', 'cat 2']), 'cat'), 'cat 3');
});

test('resolveProjectByName: exact beats prefix, ambiguity and misses are notes', () => {
  const app = (names) => ({ storage: { store: { list: () => names.map((name, i) => ({ id: i, name })) } } });
  // Exact match wins even when it is also a prefix of another name.
  assert.deepStrictEqual(resolveProjectByName(app(['cat', 'cathedral']), 'cat').meta, { id: 0, name: 'cat' });
  // A unique case-insensitive prefix resolves…
  assert.deepStrictEqual(resolveProjectByName(app(['Cathedral', 'dog']), 'cat').meta, { id: 0, name: 'Cathedral' });
  // …but an ambiguous one asks for the full name instead of guessing.
  assert.strictEqual(resolveProjectByName(app(['cathedral', 'cattle']), 'cat').note,
    '"cat" matches 2 projects — use the full name');
  assert.strictEqual(resolveProjectByName(app(['dog']), 'cat').note, 'no saved project named "cat"');
});

// ── The REAL injected capabilities, captured off sharedChatController ───────
// Same capture trick as the clearChatConversation test in ctx-assistant.test.js:
// `create` is the test seam, so the closures under test are the production ones.
const captureCapabilities = (app) => {
  let captured;
  sharedChatController(app, { create: (opts) => { captured = opts; return {}; } });
  forgetChatController(app);
  return captured;
};

test('exportImage refuses an empty editor with words, not a canvas TypeError', async () => {
  const app = { image: null, export: { renderExportCanvas: () => ({ toDataURL: () => 'data:image/png;base64,AAA' }) } };
  const caps = captureCapabilities(app);
  await assert.rejects(caps.exportImage(), /load or create one first/);
  app.image = {};
  assert.strictEqual(await caps.exportImage(), 'data:image/png;base64,AAA');
});

test('saveProject promotes to a FRESH project under a unique name and returns it', async () => {
  const calls = [];
  const app = {
    image: {}, imageBaseName: 'photo', activeProjectId: 7,
    storage: {
      store: { nameExists: (n) => n === 'cat' },
      promoteTemporaryToProject: () => calls.push('promote'),
      save: () => calls.push('save'),
    },
    renameProject: (id, name) => { calls.push(`rename ${id} ${name}`); return true; },
    updateProjectTitle: () => calls.push('title'),
  };
  const caps = captureCapabilities(app);
  // The wanted name clashes → the suffixed one lands everywhere, and comes back.
  assert.strictEqual(await caps.saveProject('cat'), 'cat 2');
  assert.strictEqual(app.imageBaseName, 'cat 2');
  assert.deepStrictEqual(calls, ['promote', 'save', 'rename 7 cat 2', 'title']);
  // No name → the editor's own base name; blank/whitespace → 'Untitled'.
  assert.strictEqual(await caps.saveProject(''), 'cat 2');
  app.imageBaseName = '   ';
  assert.strictEqual(await caps.saveProject(null), 'Untitled');
  // Nothing loaded → a thrown message, so the op reports rather than saves air.
  app.image = null;
  await assert.rejects(caps.saveProject('x'), /no image to save/);
});

test('removeProjectNamed: unknown → note, declined → note, accepted → removed', async () => {
  let allow = false;
  const removed = [];
  const confirms = [];
  const app = {
    storage: { store: { list: () => [{ id: 3, name: 'cat' }] } },
    confirm: async (msg, opts) => { confirms.push({ msg, opts }); return allow; },
    removeProject: (id) => removed.push(id),
  };
  const caps = captureCapabilities(app);
  assert.strictEqual(await caps.removeProjectNamed('dog'), 'no saved project named "dog"');
  assert.strictEqual(confirms.length, 0, 'nothing to confirm for a miss');
  assert.strictEqual(await caps.removeProjectNamed('cat'), 'removal canceled');
  assert.deepStrictEqual(removed, [], 'declined removes nothing');
  assert.match(confirms[0].msg, /Remove project "cat"\?/);
  assert.strictEqual(confirms[0].opts.danger, true);
  allow = true;
  assert.strictEqual(await caps.removeProjectNamed('cat'), null);
  assert.deepStrictEqual(removed, [3]);
});

test('clearWorkingImage words the confirm for what actually goes, and keeps the chat', async () => {
  let allow = true;
  const confirms = [];
  const editors = [];
  const app = {
    image: null, lines: [],
    storage: { incognito: false },
    confirm: async (msg) => { confirms.push(msg); return allow; },
    newEditor: (opts) => editors.push(opts),
  };
  const caps = captureCapabilities(app);
  // Empty editor: nothing to confirm, nothing to do.
  assert.strictEqual(await caps.clearWorkingImage(), 'nothing to remove');
  assert.strictEqual(confirms.length, 0);
  // An unsaved editor names the unsaved image; an incognito one says so instead.
  app.image = {};
  assert.strictEqual(await caps.clearWorkingImage(), null);
  assert.match(confirms[0], /the unsaved image and its lines/);
  assert.deepStrictEqual(editors, [{ keepChat: true }], 'mid-turn: the chat survives the clear');
  app.storage.incognito = true;
  allow = false;
  assert.strictEqual(await caps.clearWorkingImage(), 'removal canceled');
  assert.match(confirms[1], /the image in this incognito editor/);
  assert.strictEqual(editors.length, 1, 'declined clears nothing');
});

test('openProjectNamed: already open / declined replace / failed switch are notes', async () => {
  let allow = false;
  let switchable = true;
  const confirms = [];
  const app = {
    activeProjectId: 1, image: {}, lines: [],
    storage: { temporary: true, store: { list: () => [{ id: 1, name: 'open' }, { id: 2, name: 'other' }] } },
    confirm: async (msg) => { confirms.push(msg); return allow; },
    switchToProject: () => switchable,
  };
  const caps = captureCapabilities(app);
  assert.strictEqual(await caps.openProjectNamed('open'), '"open" is already open');
  // A dirty unsaved temporary asks the modal's confirm first; declined = a note.
  assert.strictEqual(await caps.openProjectNamed('other'), 'open canceled');
  assert.match(confirms[0], /unsaved changes in the current tab will be replaced/);
  allow = true;
  assert.strictEqual(await caps.openProjectNamed('other'), null);
  // A saved (non-temporary) editor switches without asking.
  app.storage.temporary = false;
  assert.strictEqual(await caps.openProjectNamed('other'), null);
  assert.strictEqual(confirms.length, 2, 'no confirm when nothing unsaved is at risk');
  switchable = false;
  assert.strictEqual(await caps.openProjectNamed('other'), 'could not open "other"');
});

test('renameActiveProject: no active project and duplicate names come back as notes', async () => {
  const app = {
    activeProjectId: null,
    storage: { store: { nameExists: (n) => n === 'taken' } },
    renameProject: (id, name) => name === 'ok',
  };
  const caps = captureCapabilities(app);
  assert.strictEqual(await caps.renameActiveProject('x'), 'no active saved project to rename');
  app.activeProjectId = 4;
  assert.strictEqual(await caps.renameActiveProject('taken'), 'a project named "taken" already exists');
  assert.strictEqual(await caps.renameActiveProject('ok'), null);
  assert.strictEqual(await caps.renameActiveProject('nope'), 'could not rename to "nope"');
});

test('setBlankColor routes saved projects through the store and gates on blankness', async () => {
  const set = [];
  const app = {
    activeProjectId: 9,
    setProjectBlankColor: (id, hex) => { set.push([id, hex]); return true; },
    activeIsBlank: () => false, image: {},
    setBlankColor: (hex) => set.push(['working', hex]),
  };
  const caps = captureCapabilities(app);
  // A saved project goes through the store setter (short #rgb normalizes to #rrggbb).
  assert.strictEqual(await caps.setBlankColor('#F00'), null);
  assert.deepStrictEqual(set, [[9, '#ff0000']]);
  // The store refusing (a non-blank project) is the §10 note.
  app.setProjectBlankColor = () => false;
  assert.strictEqual(await caps.setBlankColor('#ff0000'), 'only a blank project has a recolourable background');
  // Unsaved: only a blank working image recolours.
  app.activeProjectId = null;
  assert.strictEqual(await caps.setBlankColor('#ff0000'), 'only a blank project has a recolourable background');
  app.activeIsBlank = () => true;
  assert.strictEqual(await caps.setBlankColor('#00ff00'), null);
  assert.deepStrictEqual(set.at(-1), ['working', '#00ff00']);
});

test('clearLocalProjects counts what goes into the confirm, and empty is a note', async () => {
  let allow = false;
  let cleared = 0;
  const confirms = [];
  const list = [];
  const app = {
    storage: { store: { list: () => list } },
    confirm: async (msg) => { confirms.push(msg); return allow; },
    clearAllProjects: () => cleared++,
  };
  const caps = captureCapabilities(app);
  assert.strictEqual(await caps.clearLocalProjects(), 'no saved projects to clear');
  list.push({ id: 1 }, { id: 2 });
  assert.strictEqual(await caps.clearLocalProjects(), 'clear canceled');
  assert.match(confirms[0], /Delete every saved local project \(2\)\?/);
  allow = true;
  assert.strictEqual(await caps.clearLocalProjects(), null);
  assert.strictEqual(cleared, 1);
});

// ── The logged-turn frame: error paths, Stop, and the spinning-row guard ────
const lastRow = () => chatLog().at(-1);

test('a typed badReply (malformed 200) settles as a plain retryable error, no card', async () => {
  resetChatLog();
  const controller = { send: async () => { throw new LlmError('malformed ollama response (no message.content)', 'badReply'); } };
  const res = await runLoggedChatTurn(controller, 'hi', { settings: { provider: 'ollama', baseUrl: 'http://x' } });
  assert.strictEqual(res.kind, 'error', 'badReply is not "unreachable" — the endpoint answered');
  const row = lastRow();
  assert.strictEqual(row.text, 'Error: malformed ollama response (no message.content)');
  assert.strictEqual(row.error, true);
  assert.strictEqual(row.card, false, 'no configure-provider card for a model-side failure');
  assert.strictEqual(row.retryText, 'hi', 'retry re-sends the same prompt');
  resetChatLog();
});

test('a server with no LLM key (kind "disabled") is a textual notice in its own words', async () => {
  resetChatLog();
  const controller = { send: async () => { throw new LlmError('LLM is not enabled on this server', 'disabled'); } };
  const res = await runLoggedChatTurn(controller, 'hi', { settings: { provider: 'stencil-server', serverUrl: 'http://s' } });
  assert.strictEqual(res.kind, 'notice');
  assert.strictEqual(lastRow().text, 'LLM is not enabled on this server', 'the typed message IS the answer');
  assert.strictEqual(lastRow().card, false);
  resetChatLog();
});

test('a transport failure renders the unreachable card with the provider named', async () => {
  resetChatLog();
  const settings = { provider: 'ollama', baseUrl: 'http://localhost:11434' };
  const controller = { send: async () => { throw new LlmError('fetch failed', 'network'); } };
  const res = await runLoggedChatTurn(controller, 'hi', { settings });
  assert.strictEqual(res.kind, 'unreachable');
  const row = lastRow();
  assert.strictEqual(row.card, true, 'card + configure CTA');
  assert.strictEqual(row.reconnect, null, 'nothing to reconnect to — the config is the cure');
  assert.strictEqual(row.text, unreachableText(settings, res.error));
  resetChatLog();
});

test('an expired stencil-server session patches the row with the reconnect URL', async () => {
  resetChatLog();
  const err = Object.assign(new LlmError('unauthorized', 'http'), { status: 401, answered: true });
  const controller = { send: async () => { throw err; } };
  const res = await runLoggedChatTurn(controller, 'hi',
    { settings: { provider: 'stencil-server', serverUrl: 'http://localhost:8090' } });
  assert.strictEqual(res.kind, 'expired');
  const row = lastRow();
  assert.strictEqual(row.card, true);
  assert.strictEqual(row.reconnect, 'http://localhost:8090', 'the card knows WHICH server to sign back into');
  assert.match(row.text, /session on localhost:8090 has expired/);
  resetChatLog();
});

test('Stop: begin hands out the AbortController whose signal the send received', async () => {
  resetChatLog();
  let seenSignal = null;
  const controller = {
    send: (text, { signal }) => new Promise((_, reject) => {
      seenSignal = signal;
      signal.addEventListener('abort', () => {
        const e = new Error('aborted'); e.name = 'AbortError'; reject(e);
      });
    }),
  };
  let abort = null;
  const p = runLoggedChatTurn(controller, 'outline it', { begin: (a) => { abort = a; } });
  assert.ok(abort instanceof AbortController, 'begin fires before the send, with the live controller');
  abort.abort();
  const res = await p;
  assert.strictEqual(seenSignal, abort.signal, 'one AbortController end to end');
  assert.strictEqual(res.kind, 'abort');
  assert.strictEqual(lastRow().text, 'Stopped.');
  assert.strictEqual(lastRow().retryText, 'outline it', 'a change of mind keeps its Retry');
  resetChatLog();
});

test('hooks run begin → onResult → cleanup, with the flag down before cleanup', async () => {
  resetChatLog();
  const order = [];
  const controller = {
    send: async () => { order.push(`send inFlight=${chatTurnInFlight()}`); return { reply: 'ok', warnings: [], results: [] }; },
  };
  const res = await runLoggedChatTurn(controller, 'hi', {
    begin: () => order.push('begin'),
    onResult: (r) => order.push(`onResult ok=${r.ok}`),
    cleanup: () => order.push(`cleanup inFlight=${chatTurnInFlight()}`),
  });
  assert.strictEqual(res.ok, true);
  // The flag is up during the send and DOWN before cleanup — a surface may send again
  // from its cleanup (Resend), and the guard must not see its own previous turn.
  assert.deepStrictEqual(order, ['begin', 'send inFlight=true', 'onResult ok=true', 'cleanup inFlight=false']);
  resetChatLog();
});

test('no row is ever left spinning: a patch failure still settles the pending row', async () => {
  resetChatLog();
  // A pathological entry (undefined) makes the success patch itself throw — the
  // belt-and-braces finally must still resolve the "…" row into a retryable error.
  const controller = { send: async () => undefined };
  let cleaned = false;
  await assert.rejects(
    runLoggedChatTurn(controller, 'hi', { cleanup: () => { cleaned = true; } }),
    TypeError);
  const row = lastRow();
  assert.strictEqual(row.pending, false, 'the dots stop');
  assert.strictEqual(row.error, true);
  assert.strictEqual(row.text, 'Error: the turn ended without an answer.');
  assert.strictEqual(row.retryText, 'hi', 'even this dead end keeps its Retry');
  assert.strictEqual(cleaned, true, 'cleanup still ran');
  assert.strictEqual(chatTurnInFlight(), false);
  resetChatLog();
});

test('consecutive logged turns thread one transcript in order', async () => {
  resetChatLog();
  let n = 0;
  const controller = { send: async () => ({ reply: `reply ${++n}`, warnings: [], results: [] }) };
  await runLoggedChatTurn(controller, 'first');
  await runLoggedChatTurn(controller, 'second');
  assert.deepStrictEqual(chatLog().map((r) => `${r.role}:${r.text}`),
    ['user:first', 'assistant:reply 1', 'user:second', 'assistant:reply 2']);
  const ids = chatLog().map((r) => r.id);
  assert.deepStrictEqual([...ids].sort((a, b) => a - b), ids, 'row ids stay monotonic across turns');
  resetChatLog();
});

// ── unreachableText fallbacks (the named-provider cases live in ctx-assistant) ──
test('unreachableText degrades gracefully without a label or a URL', () => {
  // An unknown provider id is shown as itself; no provider at all → "the assistant".
  assert.ok(unreachableText({ provider: 'mystery', baseUrl: 'http://h' }, new Error('x'))
    .startsWith("Couldn't reach mystery at h"));
  assert.strictEqual(unreachableText(null, new Error('x')),
    "Couldn't reach the assistant (x)");
  // No URL → no dangling " at ", and a bare non-Error reason still reads.
  assert.strictEqual(unreachableText({ provider: 'openai-compat', baseUrl: '' }, 'ECONNREFUSED'),
    "Couldn't reach OpenAI API (LM Studio, vLLM, …) (ECONNREFUSED)");
});
