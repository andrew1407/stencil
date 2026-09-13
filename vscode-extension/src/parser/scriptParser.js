// Port of core/script/scriptParser.cpp — tokens to statements, grouped into raw blocks.
import { didYouMean, makeDiag, tokenOfStmt } from './scriptDiagnostics.js';
import { DIRECTIVES, MAX_BLOCKS, MAX_TEMPLATES, unquoteWord } from './scriptTypes.js';

const isNewline = (t) => t.kind === 'punct' && (t.text === '\n' || t.text === ';');

// The name is every word before the ':' — 'lines and rect' is one name.
const joinName = (args) =>
  args.filter((t) => t.kind !== 'punct').map((t) => unquoteWord(t.text)).join(' ');

const highestParam = (body) => {
  let top = 0;
  for (const s of body) {
    for (const t of s.args) {
      if (t.kind !== 'param') continue;
      const n = parseInt(t.text.slice(1), 10);
      if (Number.isFinite(n) && n > top) top = n;
    }
  }
  return top;
};

export const parseScript = (tokens) => {
  const diagnostics = [];
  const templates = [];
  const stmts = [];

  // Pass 1 — tokens to statements.
  let i = 0;
  while (i < tokens.length) {
    while (i < tokens.length && (isNewline(tokens[i]) || tokens[i].kind === 'comment')) i += 1;
    if (i >= tokens.length) break;

    const head = tokens[i];
    const st = { directive: '', args: [], opensBlock: false, line: head.line, col: head.col, len: 0 };

    if (head.kind !== 'directive') {
      diagnostics.push(makeDiag('error', 'E_BAD_TOKEN', head,
        `expected a directive starting with '@', found '${head.text}'`));
      while (i < tokens.length && !isNewline(tokens[i])) i += 1;
      continue;
    }

    st.directive = head.text.slice(1).toLowerCase();
    st.len = head.len;
    i += 1;

    if (!DIRECTIVES.includes(st.directive)) {
      const near = didYouMean(st.directive, DIRECTIVES);
      diagnostics.push(makeDiag('error', 'E_UNKNOWN_DIRECTIVE', head,
        `unknown directive '@${st.directive}'${near ? ` — did you mean '@${near}'?` : ''}`));
      while (i < tokens.length && !isNewline(tokens[i])) i += 1;
      continue;
    }

    while (i < tokens.length && !isNewline(tokens[i])) {
      const t = tokens[i];
      if (t.kind === 'comment') { i += 1; continue; }
      if (t.kind === 'punct' && t.text === ':') {
        // Only a ':' with nothing after it opens a block, so 'aspect=3:2' keeps its own.
        let k = i + 1;
        while (k < tokens.length && tokens[k].kind === 'comment') k += 1;
        if (k >= tokens.length || isNewline(tokens[k])) {
          st.opensBlock = true;
          i += 1;
          continue;
        }
      }
      if (st.opensBlock) {
        diagnostics.push(makeDiag('warning', 'W_TRAILING_TOKENS', t,
          `'${t.text}' after the block's ':' is ignored`));
      } else {
        st.args.push(t);
      }
      i += 1;
    }
    stmts.push(st);
  }

  // Pass 2 — statements into blocks and template definitions.
  const implicitBlock = { header: null, body: [], implicit: true };
  const sourceBlocks = [];
  let currentBlock = -1;
  let currentTemplate = -1;
  let bodyColumn = 0;
  let headerColumn = 0;

  for (const st of stmts) {
    const isHeader = st.directive === 'stencil' || st.directive === 'source';
    if (!isHeader && bodyColumn > 0 && st.col <= headerColumn) {
      currentTemplate = -1;
      currentBlock = -1;
      bodyColumn = 0;
    }

    if (st.directive === 'stencil') {
      currentTemplate = -1;
      if (!st.opensBlock) {
        diagnostics.push(makeDiag('error', 'E_MISSING_COLON', tokenOfStmt(st),
          "'@stencil <name>:' needs a trailing ':'"));
        continue;
      }
      const name = joinName(st.args);
      if (!name) {
        diagnostics.push(makeDiag('error', 'E_ARG_COUNT', tokenOfStmt(st),
          "'@stencil' needs a name before the ':'"));
        continue;
      }
      if (templates.some((d) => d.name === name)) {
        diagnostics.push(makeDiag('error', 'E_DUPLICATE_TEMPLATE', tokenOfStmt(st),
          `template '${name}' is already defined`));
        continue;
      }
      if (templates.length >= MAX_TEMPLATES) {
        diagnostics.push(makeDiag('error', 'E_LIMIT_TEMPLATES', tokenOfStmt(st), 'too many templates'));
        continue;
      }
      templates.push({ name, body: [], arity: 0, line: st.line, col: st.col, len: st.len, used: false });
      currentTemplate = templates.length - 1;
      headerColumn = st.col;
      bodyColumn = 0;
      continue;
    }

    if (st.directive === 'source') {
      if (!st.opensBlock) {
        diagnostics.push(makeDiag('error', 'E_MISSING_COLON', tokenOfStmt(st),
          "'@source <path|url>:' needs a trailing ':'"));
        continue;
      }
      if (sourceBlocks.length >= MAX_BLOCKS) {
        diagnostics.push(makeDiag('error', 'E_LIMIT_BLOCKS', tokenOfStmt(st), 'too many @source blocks'));
        continue;
      }
      sourceBlocks.push({ header: st, body: [], implicit: false });
      currentBlock = sourceBlocks.length - 1;
      currentTemplate = -1;
      headerColumn = st.col;
      bodyColumn = 0;
      continue;
    }

    if (bodyColumn === 0 && (currentTemplate >= 0 || currentBlock >= 0) && st.col > headerColumn) {
      bodyColumn = st.col;
    }
    if (currentTemplate >= 0) {
      templates[currentTemplate].body.push(st);
      continue;
    }
    if (currentBlock >= 0) sourceBlocks[currentBlock].body.push(st);
    else implicitBlock.body.push(st);
  }

  for (const d of templates) d.arity = highestParam(d.body);

  const blocks = [];
  if (implicitBlock.body.length > 0) blocks.push(implicitBlock);
  for (const b of sourceBlocks) blocks.push(b);
  return { blocks, templates, diagnostics };
};
