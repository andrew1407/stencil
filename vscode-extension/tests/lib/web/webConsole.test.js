// The console route: which browser is launched, what expression is sent, and the two waits
// that keep a run from racing a page that is still booting. Nothing here opens a browser.
import test from 'node:test';
import assert from 'node:assert/strict';

import { installVscodeStub, makeVscode } from '../../helpers/vscodeStub.js';

const APP = 'http://localhost:8080/';

const withHost = async (options, body) => {
  const { vscode, calls } = makeVscode(options);
  const host = installVscodeStub(vscode);
  try {
    return await body({ calls, vscode, webConsole: await host.import('lib/web/console.js') });
  } finally {
    host.restore();
  }
};

const session = (answers) => ({
  name: 'Stencil Web',
  requests: [],
  customRequest(command, args) {
    this.requests.push({ command, args });
    const answer = answers.shift();
    if (answer instanceof Error) return Promise.reject(answer);
    return Promise.resolve(answer ?? { result: 'undefined' });
  },
});

test('the debug configuration launches the configured browser at the instance', async () => {
  await withHost({}, ({ vscode, webConsole }) => {
    assert.deepEqual(webConsole.debugConfigFor(vscode, 'http://localhost:8080/'), {
      type: 'chrome', request: 'launch', name: 'Stencil Web', url: 'http://localhost:8080/',
    });
  });
  await withHost({ settings: { 'stencil.webBrowser': 'edge' } }, ({ vscode, webConsole }) => {
    assert.equal(webConsole.debugConfigFor(vscode, 'http://x/').type, 'msedge');
  });
  // A setting nobody recognises still opens something, rather than failing at launch.
  await withHost({ settings: { 'stencil.webBrowser': 'lynx' } }, ({ vscode, webConsole }) => {
    assert.equal(webConsole.debugConfigFor(vscode, 'http://x/').type, 'chrome');
  });
});

test('a .stc is QUOTED into one facade call — never spliced into the source', async () => {
  await withHost({}, ({ webConsole }) => {
    // The nastiest thing a script can hold: a quote, a backslash and a terminator.
    const script = '@source "a\\b.png":\n  @filter sepia\n';
    const expression = webConsole.expressionFor(script, { script: true });
    assert.equal(expression, `await window.stencil.execScript(${JSON.stringify(script)})`);
    // It round-trips as DATA: evaluating the argument literal gives back the script.
    const literal = expression.slice(expression.indexOf('(') + 1, -1);
    assert.equal(JSON.parse(literal), script);
  });
});

test('JavaScript runs as written — a repl evaluate answers with its last expression', async () => {
  await withHost({}, ({ webConsole }) => {
    assert.equal(webConsole.expressionFor('stencil.imageSize'), 'stencil.imageSize');
    assert.equal(webConsole.expressionFor('await stencil.blank("#fff");\nstencil.lines.length'),
      'await stencil.blank("#fff");\nstencil.lines.length');
    assert.equal(webConsole.expressionFor(undefined), '');
  });
});

test('an image is loaded by its string, whether that is a URL or inlined bytes', async () => {
  await withHost({}, ({ webConsole }) => {
    assert.equal(webConsole.loadExpression('https://cdn.example/i.png'),
      'await window.stencil.load("https://cdn.example/i.png")');
    assert.equal(webConsole.loadExpression('data:image/png;base64,AAAA'),
      'await window.stencil.load("data:image/png;base64,AAAA")');
  });
});

test('evaluate goes out as one repl request, and is bounded', async () => {
  await withHost({}, async ({ webConsole }) => {
    const live = session([{ result: '5' }]);
    assert.deepEqual(await webConsole.evaluate(live, '2 + 3'), { result: '5' });
    assert.deepEqual(live.requests, [
      { command: 'evaluate', args: { expression: '2 + 3', context: 'repl' } },
    ]);
    // A session that never answers must not hold the command for ever.
    const silent = { name: 'quiet', requests: [], customRequest() { return new Promise(() => {}); } };
    await assert.rejects(() => webConsole.evaluate(silent, '1', { timeoutMs: 50 }), /did not answer/);
    assert.equal(await webConsole.answersFacade(silent, { timeoutMs: 50 }), false);
  });
});

/* The bug a live run found: js-debug's LAUNCHER is the active session first and answers an
 * evaluate never — not an error, nothing — while the page arrives later as its child. */
test('the page is found even though the launcher answers nothing', async () => {
  // childDelayMs: the page really does arrive after the launcher, so prove the wait too.
  await withHost({ debugAnswers: [{ result: "'object'" }], childDelayMs: 40 },
    async ({ calls, vscode, webConsole }) => {
      const { session: page, reason } = await webConsole.pageSession(vscode, APP, { timeoutMs: 3000 });
      assert.equal(reason, undefined);
      assert.ok(page, 'the child session answered');
      assert.match(page.name, /page/);
      const [launcher] = calls.sessions;
      assert.equal(launcher.silent, true, 'and the launcher was asked, and said nothing');
    });
});

test('one session is found, then reused — a second run opens no second browser', async () => {
  await withHost({ debugAnswers: [{ result: "'object'" }, { result: "'object'" }] },
    async ({ calls, vscode, webConsole }) => {
      const first = await webConsole.pageSession(vscode, APP, { timeoutMs: 2000 });
      assert.ok(first.session);
      assert.equal(calls.debugConfigs.length, 1);
      // js-debug makes the page the active session once it is up; a second run reuses it.
      vscode.debug.activeDebugSession = first.session;
      const second = await webConsole.pageSession(vscode, APP, { timeoutMs: 2000 });
      assert.equal(second.session, first.session);
      assert.equal(calls.debugConfigs.length, 1, 'no second launch');
    });
});

test('a browser that will not start says so, once, and asks nothing', async () => {
  await withHost({ startDebugging: false }, async ({ calls, vscode, webConsole }) => {
    const { session, reason } = await webConsole.pageSession(vscode, APP, { timeoutMs: 200 });
    assert.equal(session, null);
    assert.equal(reason, webConsole.NO_SESSION);
    assert.equal(calls.debugConfigs.length, 1);
    assert.equal(calls.sessions.length, 0);
  });
});

test('a browser that starts but never boots the page is named differently', async () => {
  // Every session answers, but never with the facade: the page loaded something else.
  await withHost({ launcherAnswers: true, debugAnswers: [] },
    async ({ vscode, webConsole }) => {
      const { session, reason } = await webConsole.pageSession(vscode, APP, { timeoutMs: 400 });
      assert.equal(session, null);
      assert.equal(reason, webConsole.NO_FACADE);
    });
});

test('a session someone else started is reused only if it IS the app', async () => {
  await withHost({ debugAnswers: [{ result: "'object'" }] }, async ({ calls, vscode, webConsole }) => {
    // Somebody's server debug session is live: it answers, but not about the facade.
    vscode.debug.activeDebugSession = {
      name: 'Debug the server',
      customRequest: () => Promise.resolve({ result: "'undefined'" }),
    };
    const { session } = await webConsole.pageSession(vscode, APP, { timeoutMs: 2000 });
    assert.equal(calls.debugConfigs.length, 1, 'ours is launched beside it');
    assert.match(session.name, /page/);
  });
});

// A page that answers the probe is the wrong page if the setting names another instance.
test('a live session at another URL is not reused, however well it answers', async () => {
  await withHost({ debugAnswers: [{ result: "'object'" }] }, async ({ calls, vscode, webConsole }) => {
    vscode.debug.activeDebugSession = {
      name: 'Stencil Web',
      configuration: { url: 'http://localhost:9999/' },
      customRequest: () => Promise.resolve({ result: "'object'" }),
    };
    const { session } = await webConsole.pageSession(vscode, APP, { timeoutMs: 2000 });
    assert.equal(calls.debugConfigs.length, 1, 'the configured instance is launched instead');
    assert.match(session.name, /page/);
  });
  await withHost({}, ({ webConsole }) => {
    const at = (url) => ({ configuration: { url } });
    assert.equal(webConsole.atUrl(at(APP), APP), true);
    assert.equal(webConsole.atUrl(at(`${APP}#stencil=x`), APP), true, 'the hand-off adds a fragment');
    assert.equal(webConsole.atUrl(at('http://localhost:9999/'), APP), false);
    assert.equal(webConsole.atUrl({ name: 'attached' }, APP), true, 'an attach declares no URL');
  });
});
