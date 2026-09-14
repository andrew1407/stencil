// `vscode` is injected by the extension host and exists nowhere on disk, so a Module._load
// hook answers require('vscode') with the stub below. Everything the extension touches is
// here and records what it was asked to do; nothing simulates the editor.
import Module from 'node:module';
import { createRequire } from 'node:module';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const SRC = resolve(dirname(fileURLToPath(import.meta.url)), '../../src');

class Position {
  constructor(line, character) { this.line = line; this.character = character; }
}

class Range {
  constructor(startLine, startChar, endLine, endChar) {
    this.start = new Position(startLine, startChar);
    this.end = new Position(endLine, endChar);
  }
}

class Diagnostic {
  constructor(range, message, severity) {
    this.range = range;
    this.message = message;
    this.severity = severity;
  }
}

class SemanticTokensBuilder {
  constructor(legend) { this.legend = legend; this.rows = []; }
  push(...row) { this.rows.push(row); }
  build() { return { rows: this.rows }; }
}

class Terminal {
  constructor(name, cwd) { this.name = name; this.cwd = cwd; this.sent = []; this.shown = 0; }
  sendText(text) { this.sent.push(text); }
  show() { this.shown += 1; }
}

/* One stub instance. `settings` seeds workspace.getConfiguration; `calls` collects what the
 * extension registered or showed, so a test can assert on it. */
export const makeVscode = ({ settings = {}, openDialog = [], shell = '/bin/sh' } = {}) => {
  const calls = {
    collections: [], commands: new Map(), events: {}, errors: [],
    semanticProviders: [], terminals: [],
  };
  const on = (name) => (handler) => {
    (calls.events[name] ??= []).push(handler);
    return { dispose() {} };
  };
  const vscode = {
    Position, Range, Diagnostic, SemanticTokensBuilder,
    env: { shell },
    DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2, Hint: 3 },
    SemanticTokensLegend: class { constructor(types, mods = []) { this.tokenTypes = types; this.tokenModifiers = mods; } },
    Uri: { file: (path) => ({ scheme: 'file', fsPath: path, toString: () => `file://${path}` }) },
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
    },
    commands: {
      registerCommand(id, handler) { calls.commands.set(id, handler); return { dispose() {} }; },
    },
    window: {
      activeTextEditor: undefined,
      terminals: calls.terminals,
      createTerminal({ name, cwd }) {
        const terminal = new Terminal(name, cwd);
        calls.terminals.push(terminal);
        return terminal;
      },
      showErrorMessage(message) { calls.errors.push(message); return Promise.resolve(undefined); },
      showOpenDialog() { return Promise.resolve(openDialog); },
    },
    workspace: {
      textDocuments: [],
      getWorkspaceFolder: () => undefined,
      getConfiguration: (section) => ({
        get: (key, fallback) => settings[`${section}.${key}`] ?? fallback,
      }),
      onDidOpenTextDocument: on('open'),
      onDidSaveTextDocument: on('save'),
      onDidCloseTextDocument: on('close'),
      onDidChangeTextDocument: on('change'),
    },
  };
  return { vscode, calls };
};

/* Installs the hook and returns a require() rooted at src/. The src/ cache is dropped first,
 * so each install binds the modules to THIS stub; call restore() when done, since the hook
 * is process-wide and node --test shares one process per file. */
export const installVscodeStub = (vscode) => {
  const original = Module._load;
  Module._load = function load(request, parent, isMain) {
    if (request === 'vscode') return vscode;
    return original.call(this, request, parent, isMain);
  };
  const req = createRequire(`${SRC}/`);
  for (const key of Object.keys(req.cache)) if (key.startsWith(SRC)) delete req.cache[key];
  return {
    require: (rel) => req(resolve(SRC, rel)),
    restore() { Module._load = original; },
  };
};

export const makeContext = () => ({ subscriptions: [] });

export const makeDocument = ({
  path = '/tmp/demo.stc', text = '', languageId = 'stencil-script', isDirty = false,
  scheme = 'file', version, save = async () => true,
} = {}) => ({
  languageId,
  isDirty,
  version,
  uri: { scheme, fsPath: path, toString: () => `${scheme}://${path}` },
  getText: () => text,
  save,
});
