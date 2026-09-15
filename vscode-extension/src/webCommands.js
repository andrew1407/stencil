// The browser commands. Two routes: a hand-off the user's own browser opens (the `#stencil=`
// fragment), and the page's console over VS Code's built-in JS debugger. Neither composes a
// shell line, and neither reads its target from the document — that is webTarget.js's job.
'use strict';

const vscode = require('vscode');

const { COMMANDS, CONFIG_SECTION, LANGUAGE_ID, PROJECT_LANGUAGE_ID, SETTINGS,
} = require('./lib/ids.js');
const { isJsSource } = require('./lib/jsSource.js');
const { BAD_WEB_URL, webUrlFor } = require('./lib/webTarget.js');
const { buildLaunchUrl, imageDataUrl, isTooBig, localSources, projectLaunch,
  scriptLaunch } = require('./lib/webLaunch.js');
const { evaluate, expressionFor, loadExpression, pageSession } = require('./lib/webConsole.js');
const { programFor } = require('./lib/programCache.js');

const OUTPUT_NAME = 'Stencil';
const OPEN_A_FILE = 'Open a .stc script, a .stcjs file or a .stencil project first';
const STCJS_IS_CONSOLE_ONLY = 'A .stcjs is JavaScript — run it with "Stencil: Run in Stencil '
  + 'Web Console". The hand-off carries scripts and pictures, never code.';
const TOO_BIG = 'Too much to put in a URL — open the picture in the app and run the script there';

let channel = null;
const output = () => (channel ??= vscode.window.createOutputChannel(OUTPUT_NAME));
const report = (line) => { output().appendLine(line); };

const activeDocument = () => vscode.window.activeTextEditor?.document ?? null;

const target = () => {
  const url = webUrlFor(vscode);
  if (!url) vscode.window.showErrorMessage(BAD_WEB_URL);
  return url;
};

// ── Route A: the hand-off ────────────────────────────────────────────────────
const inlineImages = () =>
  vscode.workspace.getConfiguration(CONFIG_SECTION).get(SETTINGS.webInlineImages, true);

const pickFile = async () => {
  const picked = await vscode.window.showOpenDialog({
    canSelectMany: false, openLabel: 'Run script on', title: 'Stencil: image to run the script on',
  });
  return picked && picked.length ? picked[0].fsPath : '';
};

/* A script that names no source acts on whatever is open — and a fresh tab holds nothing, so
 * it brings a picture with it. A cancelled pick still hands the script over: the app reports
 * what it could not do, which is the answer the user asked for. */
const scriptLaunchFor = async (document) => {
  const script = document.getText();
  const { blocks } = await programFor(document);
  const local = localSources(blocks);
  // Sent anyway: the app has no filesystem and says so itself, on the line it happened.
  if (local.length) {
    vscode.window.showWarningMessage(
      `The browser cannot open ${local[0]} — a @source there must be an http(s) URL`);
  }
  // Every script has at least the project block; only a named @source brings its own picture.
  if (blocks.some((block) => block.source)) return scriptLaunch(script);
  const image = await pickFile();
  return image ? scriptLaunch(script, image, { inline: inlineImages() }) : scriptLaunch(script);
};

// What each file type becomes in the fragment; anything else is not a hand-off at all.
const LAUNCH_FOR = Object.freeze({
  [LANGUAGE_ID]: scriptLaunchFor,
  [PROJECT_LANGUAGE_ID]: (document) => projectLaunch(document.getText()),
});

const launchFor = (document) => LAUNCH_FOR[document.languageId]?.(document) ?? null;

const openInWeb = async () => {
  const document = activeDocument();
  if (!document) return vscode.window.showErrorMessage(OPEN_A_FILE);
  if (isJsSource(document)) return vscode.window.showErrorMessage(STCJS_IS_CONSOLE_ONLY);
  const url = target();
  if (!url) return undefined;
  const payload = await launchFor(document);
  if (!payload) return vscode.window.showErrorMessage(OPEN_A_FILE);
  const launch = buildLaunchUrl(url, payload);
  if (isTooBig(launch)) return vscode.window.showErrorMessage(TOO_BIG);
  return vscode.env.openExternal(vscode.Uri.parse(launch));
};

// ── Route B: the console ─────────────────────────────────────────────────────
/* One live session, reused; the first call also waits for the page to finish booting so the
 * facade is there to answer. Every result and every failure lands in the output channel. */
const runExpression = async (expression, opts) => {
  const url = target();
  if (!url) return undefined;
  const { session, reason } = await pageSession(vscode, url, opts);
  if (!session) {
    vscode.window.showErrorMessage(reason);
    return undefined;
  }
  output().show(true);
  report(`> ${expression}`);
  try {
    const answer = await evaluate(session, expression);
    report(String(answer?.result ?? 'ok'));
    return answer;
  } catch (err) {
    report(`error: ${err?.message ?? err}`);
    return undefined;
  }
};

const sourceExpression = (document, text) => expressionFor(text, {
  script: document.languageId === LANGUAGE_ID,
});

const runInWebConsole = async () => {
  const document = activeDocument();
  if (!document || (document.languageId !== LANGUAGE_ID && !isJsSource(document))) {
    return vscode.window.showErrorMessage(OPEN_A_FILE);
  }
  return runExpression(sourceExpression(document, document.getText()));
};

const runSelectionInWebConsole = async () => {
  const editor = vscode.window.activeTextEditor;
  const selected = editor && !editor.selection?.isEmpty
    ? editor.document.getText(editor.selection) : '';
  if (selected) return runExpression(sourceExpression(editor.document, selected));
  const typed = await vscode.window.showInputBox({
    title: 'Stencil: run in the browser console',
    prompt: 'A JavaScript expression against the live editor',
    placeHolder: 'stencil.rotateRight().apply({ filter: "sepia" })',
  });
  return typed ? runExpression(expressionFor(typed)) : undefined;
};

const pickImage = async () => {
  const typed = await vscode.window.showInputBox({
    title: 'Stencil: open an image in the browser',
    prompt: 'An http(s) image URL — leave empty to pick a file',
  });
  if (typed) return typed;
  if (typed === undefined) return '';   // dismissed, not "pick a file instead"
  const picked = await vscode.window.showOpenDialog({
    canSelectMany: false, openLabel: 'Open in Stencil', title: 'Stencil: image to open',
  });
  if (!picked || !picked.length) return '';
  if (!inlineImages()) {
    vscode.window.showErrorMessage('Turn on stencil.webInlineImages to open a local file');
    return '';
  }
  return imageDataUrl(picked[0].fsPath)?.dataUrl ?? '';
};

const openImageInWeb = async () => {
  const image = await pickImage();
  return image ? runExpression(loadExpression(image)) : undefined;
};

const HANDLERS = Object.freeze({
  [COMMANDS.openInWeb]: openInWeb,
  [COMMANDS.runInWebConsole]: runInWebConsole,
  [COMMANDS.runSelectionInWebConsole]: runSelectionInWebConsole,
  [COMMANDS.openImageInWeb]: openImageInWeb,
});

const register = (context) => {
  channel?.dispose();
  channel = null;
  for (const [id, handler] of Object.entries(HANDLERS)) {
    context.subscriptions.push(vscode.commands.registerCommand(id, handler));
  }
  context.subscriptions.push({ dispose: () => { channel?.dispose(); channel = null; } });
  return HANDLERS;
};

module.exports = {
  HANDLERS, OPEN_A_FILE, OUTPUT_NAME, STCJS_IS_CONSOLE_ONLY, TOO_BIG, openImageInWeb,
  openInWeb, register, runExpression, runInWebConsole, runSelectionInWebConsole,
};
