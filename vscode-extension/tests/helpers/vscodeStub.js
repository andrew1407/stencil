// `vscode` is injected by the extension host and exists nowhere on disk, so a resolve hook
// answers `import 'vscode'` with the stub below. Everything the extension touches is
// here and records what it was asked to do; nothing simulates the editor.
import {
  CompletionItem, DebugSession, Diagnostic, Hover, MarkdownString, OutputChannel, Position,
  Range, SemanticTokensBuilder, Terminal,
} from './vscodeTypes.js';
import { registerHooks } from 'node:module';

const SRC = new URL('../../src/', import.meta.url).href;
const GENERATION = /\?stub=(\d+)$/;
const stubs = new Map();
let generation = 0;

// Every src/ URL carries its install's generation, so each install loads a fresh module graph.
registerHooks({
  resolve(specifier, context, next) {
    const gen = context.parentURL?.match(GENERATION)?.[1];
    if (gen && specifier === 'vscode') {
      const names = Object.keys(stubs.get(Number(gen)));
      const source = names.map((n) => `export const ${n} = globalThis.__vscodeStubs.get(${gen}).${n};`);
      return { url: `data:text/javascript,${encodeURIComponent(source.join('\n'))}`, shortCircuit: true };
    }
    const resolved = next(specifier, context);
    if (!gen || !resolved.url.startsWith(SRC) || GENERATION.test(resolved.url)) return resolved;
    return { ...resolved, url: `${resolved.url}?stub=${gen}` };
  },
});
globalThis.__vscodeStubs = stubs;

// `settings` seeds getConfiguration; `calls` collects what was registered or shown.
export const makeVscode = ({
  settings = {}, openDialog = [], shell = '/bin/sh', themeKind = 2,
  inputBox = [], quickPick = [], debugAnswers = [], startDebugging = true, launcherAnswers = false,
  childDelayMs = 0, workspaceFolder = '',
} = {}) => {
  const calls = {
    collections: [], commands: new Map(), completionProviders: [], errors: [], events: {},
    decorationTypes: [], editors: [], executed: [], hoverProviders: [], semanticProviders: [],
    terminals: [], updates: [], opened: [], warnings: [], infos: [], channels: [],
    debugConfigs: [], sessions: [], picks: [],
  };
  const on = (name) => (handler) => {
    (calls.events[name] ??= []).push(handler);
    return { dispose() {} };
  };
  const vscode = {
    Position, Range, Diagnostic, SemanticTokensBuilder,
    MarkdownString, Hover, CompletionItem,
    // The real enum is much longer; these are the members the completion items name.
    CompletionItemKind: { Keyword: 13, EnumMember: 19, Property: 9, Field: 4, Unit: 10, Color: 15, Function: 2 },
    ColorThemeKind: { Light: 1, Dark: 2, HighContrast: 3, HighContrastLight: 4 },
    DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2, Hint: 3 },
    SemanticTokensLegend: class { constructor(types, mods = []) { this.tokenTypes = types; this.tokenModifiers = mods; } },
    Uri: {
      file: (path) => ({ scheme: 'file', fsPath: path, toString: () => `file://${path}` }),
      parse: (value) => ({ scheme: String(value).split(':')[0], toString: () => String(value) }),
    },
    env: {
      shell,
      openExternal(uri) { calls.opened.push(String(uri)); return Promise.resolve(true); },
    },
    debug: {
      activeDebugSession: undefined,
      onDidStartDebugSession: on('debugSession'),
      // What js-debug does: the launcher goes active, the PAGE arrives later as its child.
      startDebugging(folder, config) {
        calls.debugConfigs.push({ folder, config });
        if (!startDebugging) return Promise.resolve(false);
        const launcher = new DebugSession(config.name, debugAnswers, { silent: !launcherAnswers, url: config.url });
        calls.sessions.push(launcher);
        vscode.debug.activeDebugSession = launcher;
        if (launcherAnswers) return Promise.resolve(true);
        const page = new DebugSession(`${config.name}: page`, debugAnswers, { url: config.url });
        calls.sessions.push(page);
        const announce = () => { for (const h of calls.events.debugSession ?? []) h(page); };
        if (childDelayMs) setTimeout(announce, childDelayMs);
        else announce();
        return Promise.resolve(true);
      },
    },
    languages: {
      createDiagnosticCollection(name) {
        const collection = { name, entries: new Map(), set(uri, list) { this.entries.set(String(uri.fsPath ?? uri), list); }, delete(uri) { this.entries.delete(String(uri.fsPath ?? uri)); }, dispose() {} };
        calls.collections.push(collection);
        return collection;
      },
      registerDocumentSemanticTokensProvider(selector, provider, legend) {
        calls.semanticProviders.push({ selector, provider, legend });
        return { dispose() {} };
      },
      registerCompletionItemProvider(selector, provider, ...triggers) {
        calls.completionProviders.push({ selector, provider, triggers });
        return { dispose() {} };
      },
      registerHoverProvider(selector, provider) {
        calls.hoverProviders.push({ selector, provider });
        return { dispose() {} };
      },
    },
    commands: {
      registerCommand(id, handler) { calls.commands.set(id, handler); return { dispose() {} }; },
      executeCommand(id, ...args) { calls.executed.push({ id, args }); return Promise.resolve(); },
    },
    window: {
      activeTextEditor: undefined,
      visibleTextEditors: calls.editors,
      terminals: calls.terminals,
      createTextEditorDecorationType(options) {
        const type = { options, disposed: false, dispose() { this.disposed = true; } };
        calls.decorationTypes.push(type);
        return type;
      },
      activeColorTheme: { kind: themeKind },
      onDidChangeVisibleTextEditors: on('visibleEditors'),
      onDidChangeActiveColorTheme: on('theme'),
      onDidChangeActiveTextEditor: on('activeEditor'),
      createTerminal({ name, cwd }) {
        const terminal = new Terminal(name, cwd);
        calls.terminals.push(terminal);
        return terminal;
      },
      showErrorMessage(message) { calls.errors.push(message); return Promise.resolve(undefined); },
      showWarningMessage(message) { calls.warnings.push(message); return Promise.resolve(undefined); },
      showInformationMessage(message) { calls.infos.push(message); return Promise.resolve(undefined); },
      showOpenDialog() { return Promise.resolve(openDialog); },
      showInputBox() { return Promise.resolve(inputBox.shift()); },
      showQuickPick(items, options) {
        calls.picks.push({ items, options });
        return Promise.resolve(quickPick.shift());
      },
      createOutputChannel(name) {
        const channel = new OutputChannel(name);
        calls.channels.push(channel);
        return channel;
      },
    },
    workspace: {
      textDocuments: [],
      workspaceFolders: workspaceFolder ? [{ uri: { fsPath: workspaceFolder } }] : undefined,
      getWorkspaceFolder: () => (workspaceFolder ? { uri: { fsPath: workspaceFolder } } : undefined),
      // getConfiguration() with no section is addressed by full id, the way colors.js reads it.
      getConfiguration: (section) => ({
        get: (key, fallback) => settings[section ? `${section}.${key}` : key] ?? fallback,
        update: (key, value, global) => {
          calls.updates.push({ key, value, global });
          settings[key] = value;
          return Promise.resolve();
        },
      }),
      onDidOpenTextDocument: on('open'),
      onDidSaveTextDocument: on('save'),
      onDidCloseTextDocument: on('close'),
      onDidChangeTextDocument: on('change'),
      onDidChangeConfiguration: on('config'),
    },
  };
  return { vscode, calls };
};

/* Binds a fresh load of src/ to THIS stub and returns its import(), rooted at src/. Call
 * restore() when done; node --test shares one process per file. */
export const installVscodeStub = (vscode) => {
  const gen = ++generation;
  stubs.set(gen, vscode);
  return {
    import: (rel) => import(`${new URL(rel, SRC).href}?stub=${gen}`),
    restore() { stubs.delete(gen); },
  };
};

export const makeContext = () => ({ subscriptions: [] });

/* A stand-in for a visible editor: it records what was painted, per decoration type. */
export const makeEditor = (document, selection) => ({
  document,
  selection,
  painted: new Map(),
  setDecorations(type, ranges) { this.painted.set(type, ranges); },
});

// What the commands read off a selection: whether it is empty, and the text it covers.
export const makeSelection = (text) => ({ isEmpty: !text, text });

export const makeDocument = ({
  path = '/tmp/demo.stc', text = '', languageId = 'stencil-script', isDirty = false,
  scheme = 'file', version, save = async () => true,
} = {}) => ({
  languageId,
  isDirty,
  version,
  uri: { scheme, fsPath: path, toString: () => `${scheme}://${path}` },
  getText: (selection) => (selection ? selection.text : text),
  // The real lineAt throws on an out-of-range line, so a provider asking for one fails here too.
  lineAt: (line) => {
    const lines = text.split(/\r?\n/);
    if (!Number.isInteger(line) || line < 0 || line >= lines.length) {
      throw new RangeError(`Illegal value for line: ${line}`);
    }
    return { text: lines[line], lineNumber: line };
  },
  save,
});
