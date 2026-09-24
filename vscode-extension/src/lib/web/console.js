// The page's own console: VS Code's BUILT-IN JS debugger opens Chrome/Edge on the instance
// in a throwaway profile, and a DAP `evaluate` runs there — DevTools, not the app.
import { CONFIG_SECTION, SETTINGS } from '../ids.js';

const SESSION_NAME = 'Stencil Web';
const BROWSERS = Object.freeze({ chrome: 'chrome', edge: 'msedge' });

const SESSION_TIMEOUT_MS = 20_000;
const POLL_MS = 150;
// js-debug's launcher session never answers `evaluate` — the PAGE is a child it starts a
// moment later — so a request must be bounded rather than awaited forever.
const EVAL_TIMEOUT_MS = 60_000;
const PROBE_TIMEOUT_MS = 1000;

const NO_SESSION = 'Could not start a browser for Stencil — check that Chrome or Edge is installed';
const NO_FACADE = 'The page never finished booting — window.stencil is not there';

const sleep = (ms) => new Promise((done) => { setTimeout(done, ms); });

const debugConfigFor = (vscode, url) => {
  const browser = vscode.workspace.getConfiguration(CONFIG_SECTION).get(SETTINGS.webBrowser, 'chrome');
  return {
    type: BROWSERS[browser] ?? BROWSERS.chrome, request: 'launch', name: SESSION_NAME, url,
  };
};

/* A .stc is DATA: quoted into one call, never spliced into source. Anything else IS
 * JavaScript, and a `repl` evaluate answers with the last expression and allows `await`. */
const expressionFor = (text, { script = false } = {}) => (script
  ? `await window.stencil.execScript(${JSON.stringify(String(text ?? ''))})`
  : String(text ?? ''));

// An image by URL or by inlined bytes — either way it is a string the facade fetches.
const loadExpression = (url) => `await window.stencil.load(${JSON.stringify(String(url ?? ''))})`;

// The timer is cleared either way: an answered request must not leave one behind.
const evaluate = (session, expression, { timeoutMs = EVAL_TIMEOUT_MS } = {}) => {
  let timer = null;
  const bound = new Promise((resolve, reject) => {
    timer = setTimeout(() => reject(new Error('the debug session did not answer')), timeoutMs);
  });
  return Promise.race([session.customRequest('evaluate', { expression, context: 'repl' }), bound])
    .finally(() => clearTimeout(timer));
};

// A session launched elsewhere is not this instance; one declaring no URL is taken at its word.
const atUrl = (session, url) => {
  const configured = session?.configuration?.url;
  return !configured || String(configured).split('#')[0] === String(url ?? '').split('#')[0];
};

// True only for a session that IS the page: the launcher answers nothing at all.
const answersFacade = async (session, { timeoutMs = PROBE_TIMEOUT_MS } = {}) => {
  const opts = { timeoutMs };
  try {
    const answer = await evaluate(session, 'typeof window.stencil', opts);
    return String(answer && answer.result).includes('object');
  } catch {
    return false;
  }
};

// Asked together, not in turn: a silent launcher must not delay the page beside it.
const firstAnswering = (sessions, opts) => new Promise((resolve) => {
  let pending = sessions.length;
  if (!pending) {
    resolve(null);
    return;
  }
  for (const session of sessions) {
    answersFacade(session, opts).then((ok) => {
      if (ok) resolve(session);
      else if ((pending -= 1) === 0) resolve(null);
    });
  }
});

// Every session this window starts, newest first — the page is a child of the launcher, and
// `activeDebugSession` may be either while the two settle.
const trackSessions = (vscode) => {
  const seen = [];
  const sub = vscode.debug.onDidStartDebugSession?.((session) => seen.unshift(session));
  return { seen, dispose: () => sub?.dispose?.() };
};

const candidates = (vscode, tracked) => {
  const live = vscode.debug.activeDebugSession;
  return [...tracked.seen, ...(live ? [live] : [])];
};

/* The session that can actually run an expression in the page. A live one that already
 * answers is reused, so a second run opens no second browser; otherwise the browser is
 * started and every session it brings is asked until one answers or the deadline passes.
 * `reason` says which of the two failures it was, so the caller can name it. */
const pageSession = async (vscode, url, { timeoutMs = SESSION_TIMEOUT_MS } = {}) => {
  // A probe can never outlast the whole wait: a silent launcher must not eat the deadline.
  const probe = { timeoutMs: Math.min(PROBE_TIMEOUT_MS, timeoutMs) };
  const live = vscode.debug.activeDebugSession;
  if (live && atUrl(live, url) && await answersFacade(live, probe)) return { session: live };
  const tracked = trackSessions(vscode);
  try {
    if (!(await vscode.debug.startDebugging(undefined, debugConfigFor(vscode, url)))) {
      return { session: null, reason: NO_SESSION };
    }
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const session = await firstAnswering(candidates(vscode, tracked), probe);
      if (session) return { session };
      if (Date.now() >= deadline) return { session: null, reason: NO_FACADE };
      await sleep(POLL_MS);
    }
  } finally {
    tracked.dispose();
  }
};

export {
  BROWSERS, EVAL_TIMEOUT_MS, NO_FACADE, NO_SESSION, POLL_MS, PROBE_TIMEOUT_MS, SESSION_NAME,
  SESSION_TIMEOUT_MS, answersFacade, atUrl, debugConfigFor, evaluate, expressionFor,
  loadExpression, pageSession,
};
