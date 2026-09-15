// Builds typings/stencil.d.ts: the facade's own types, flattened into ONE ambient script so
// a workspace can drop it beside its JavaScript and have the editor type `stencil` itself.
// The shape comes from browser/js/console/stencilApi.d.ts, the prose from
// src/config/stencilApiVocabulary.json; tests/typings.test.js regenerates and compares, so
// neither can drift. Run: node tools/genTypings.mjs
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const HERE = dirname(fileURLToPath(import.meta.url));
const SOURCE = join(HERE, '..', '..', 'browser', 'js', 'console', 'stencilApi.d.ts');
const VOCABULARY = join(HERE, '..', 'src', 'config', 'stencilApiVocabulary.json');
const OUT = join(HERE, '..', 'typings', 'stencil.d.ts');

export const DOCS_URL = 'https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api';
export const CONTRACT_URL = 'https://github.com/andrew1407/stencil/blob/main/contracts/stc/stc-contract.md';

// The types stencilApi.d.ts imports from the app. A hover needs their NAMES, not their
// bodies, so each becomes an opaque alias rather than dragging in eight more modules.
const OPAQUE = ['DrawingApp', 'CodecLine', 'LayoutPayload', 'WireCropRect', 'RefreshPeriod',
  'ConnectSpec', 'TaggedRemoteProject', 'RemoteProjectMeta', 'LlmSettings', 'VariantResult'];

const MEMBER = /^(?:readonly\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\??\s*[(:]/;
const DOC_LINE = /^\/\*\*.*\*\/$/;

// A JSDoc block for one member: what it is, what it hands back, how it is used, and where to
// read more. The link renders as a link, the way lib.dom.d.ts's "MDN Reference" does.
const docFor = (name, entry) => {
  const lines = [`${entry.summary}`];
  if (entry.detail) lines.push('', entry.detail);
  if (entry.example) lines.push('', '```js', ...entry.example.split('\n'), '```');
  lines.push('', `[Stencil console API](${DOCS_URL})`);
  return ['  /**', ...lines.map((line) => (line ? `   * ${line}` : '   *')), '   */'].join('\n');
};

export const buildTypings = ({ source = readFileSync(SOURCE, 'utf8'),
  vocabulary = JSON.parse(readFileSync(VOCABULARY, 'utf8')) } = {}) => {
  const { members } = vocabulary;
  const out = [];
  let inFacade = false;
  let pendingDoc = -1;

  for (const raw of source.split('\n')) {
    const line = raw.replace(/\s+$/, '');
    // The imports become opaque aliases, once, where the first one stood.
    if (/^import type /.test(line)) {
      if (!out.some((l) => l.startsWith('type DrawingApp'))) {
        out.push(...OPAQUE.map((name) => `type ${name} = unknown;`), '');
      }
      continue;
    }
    // A re-export and the factory belong to the app, not to a workspace's typings.
    if (/^export \{ WINDOWS \}/.test(line) || /^export declare const createStencil/.test(line)) continue;
    if (/^\/\*\* Builds the facade once/.test(line)) continue;

    if (/^export interface (Stencil|StencilSettings)\b/.test(line)) inFacade = true;
    else if (inFacade && line === '}') inFacade = false;

    if (inFacade) {
      const trimmed = line.trim();
      // The source's own one-line doc is folded into the vocabulary's `detail`, so it goes.
      if (DOC_LINE.test(trimmed)) { pendingDoc = out.length; continue; }
      const match = MEMBER.exec(trimmed);
      if (match && members[match[1]]) {
        if (pendingDoc >= 0) pendingDoc = -1;
        out.push(docFor(match[1], members[match[1]]));
      }
    }
    out.push(line.replace(/^export /, ''));
  }

  const banner = [
    '// The Stencil browser app\'s console API, as types.',
    '//',
    '// Generated from browser/js/console/stencilApi.d.ts by vscode-extension/tools/genTypings.mjs.',
    '// Drop it beside your JavaScript and the editor types `stencil` itself: hovering a member',
    '// gives its signature rather than `any`. The Stencil extension writes it here for you with',
    '// "Stencil: Add facade typings to this workspace".',
    '',
  ].join('\n');

  const global = [
    '/**',
    ' * The browser app\'s console control API, installed by the page as `window.stencil`.',
    ' *',
    ' * A frozen, guarded facade over the live editor: every mutation goes through the same core',
    ' * methods the toolbar uses, and most calls hand the facade back, so they chain.',
    ' *',
    ' * ```js',
    ' * stencil.rotateRight().apply({ filter: \'sepia\' });',
    ' * ```',
    ' *',
    ` * [Stencil console API](${DOCS_URL}) · [the .stc language](${CONTRACT_URL})`,
    ' */',
    // `var`, not `const`, so it reads as the ambient global it is: the same declaration
    // lib.dom.d.ts gives `window`, and so the same colour and the same `var …:` hover.
    'declare var stencil: Stencil;',
    '',
  ].join('\n');

  return `${banner}${out.join('\n').replace(/\n{3,}/g, '\n\n').trim()}\n\n${global}`;
};

if (process.argv[1] && process.argv[1].endsWith('genTypings.mjs')) {
  mkdirSync(dirname(OUT), { recursive: true });
  writeFileSync(OUT, buildTypings());
  console.log(`wrote ${OUT}`);
}
