// The classes the extension constructs through `vscode`; vscodeStub.js is the wiring.
export class Position {
  constructor(line, character) { this.line = line; this.character = character; }
}

export class Range {
  constructor(startLine, startChar, endLine, endChar) {
    this.start = new Position(startLine, startChar);
    this.end = new Position(endLine, endChar);
  }
}

export class Diagnostic {
  constructor(range, message, severity) {
    this.range = range;
    this.message = message;
    this.severity = severity;
  }
}

export class SemanticTokensBuilder {
  constructor(legend) { this.legend = legend; this.rows = []; }
  push(...row) { this.rows.push(row); }
  build() { return { rows: this.rows }; }
}

export class MarkdownString {
  constructor(value = '') { this.value = value; this.supportHtml = false; }
}

export class Hover {
  constructor(contents, range) { this.contents = contents; this.range = range; }
}

export class CompletionItem {
  constructor(label, kind) { this.label = label; this.kind = kind; }
}

export class OutputChannel {
  constructor(name) { this.name = name; this.lines = []; this.shown = 0; this.disposed = false; }
  appendLine(line) { this.lines.push(line); }
  show() { this.shown += 1; }
  dispose() { this.disposed = true; }
}

// A seeded answer may be an Error — how a page-side throw arrives.
export class DebugSession {
  constructor(name, answers, { silent = false, url = '' } = {}) {
    this.name = name;
    this.answers = answers;
    this.requests = [];
    this.silent = silent;
    this.configuration = { url };   // what a real launch session carries, and which instance
  }

  customRequest(command, args) {
    this.requests.push({ command, args });
    // js-debug's launcher never answers at all.
    if (this.silent) return new Promise(() => {});
    const answer = this.answers.shift();
    if (answer instanceof Error) return Promise.reject(answer);
    return Promise.resolve(answer ?? { result: 'undefined' });
  }
}

export class Terminal {
  constructor(name, cwd) { this.name = name; this.cwd = cwd; this.sent = []; this.shown = 0; }
  sendText(text) { this.sent.push(text); }
  show() { this.shown += 1; }
}
