// Explicit per-family colours. Semantic tokens carry the default look, but a theme — not an
// extension — decides what a token type is painted, so an exact colour has to be drawn on top
// as a decoration. Only the families named in `stencil.colors` get one; the rest stay themed.
import * as vscode from 'vscode';

import { CONFIG_SECTION, LANGUAGE_ID, SETTINGS } from './lib/ids.js';
import { overridesFor } from './lib/vocab/colorFamilies.js';
import { programFor } from './lib/programCache.js';
import { unitSpan } from './lib/spans.js';
import { classify } from './lib/vocab/tokenClassify.js';

const DEBOUNCE_MS = 200;

const setting = () => vscode.workspace.getConfiguration(CONFIG_SECTION).get(SETTINGS.colors, {});

// Which palette the defaults come from; a high-contrast light theme is a light one.
const onLight = () => {
  const { Light, HighContrastLight } = vscode.ColorThemeKind ?? {};
  const kind = vscode.window.activeColorTheme?.kind;
  return kind === Light || kind === HighContrastLight;
};

/// type → the UTF-16 ranges its tokens occupy on `lines`, for the types being overridden.
const rangesFor = (tokens, types, lines = []) => {
  const painted = classify(tokens);
  const out = new Map(types.map((type) => [type, []]));
  (tokens ?? []).forEach((token, i) => {
    const list = out.get(painted[i]);
    if (!list || !(token.len > 0)) return;
    const { line, start, end } = unitSpan(lines, token);
    list.push(new vscode.Range(line, start, line, end));
  });
  return out;
};

const register = (context) => {
  let types = new Map();          // legend type → DecorationType, rebuilt when the setting changes

  const rebuild = () => {
    for (const type of types.values()) type.dispose();
    types = new Map(Object.entries(overridesFor(setting(), { light: onLight() }))
      .map(([type, color]) => [type, vscode.window.createTextEditorDecorationType({ color })]));
  };

  const paint = async (editor) => {
    if (!editor || editor.document.languageId !== LANGUAGE_ID) return;
    if (types.size === 0) return;
    const { version } = editor.document;
    const program = await programFor(editor.document);
    if (editor.document.version !== version) return;
    const ranges = rangesFor(program.tokens, [...types.keys()], program.lines);
    for (const [type, decoration] of types) editor.setDecorations(decoration, ranges.get(type));
  };

  const paintAll = () => { for (const editor of vscode.window.visibleTextEditors) paint(editor); };

  let timer;
  const later = () => {
    clearTimeout(timer);
    timer = setTimeout(paintAll, DEBOUNCE_MS);
    timer.unref?.();
  };

  rebuild();
  paintAll();
  context.subscriptions.push(
    { dispose() { for (const type of types.values()) type.dispose(); } },
    vscode.window.onDidChangeVisibleTextEditors(paintAll),
    vscode.window.onDidChangeActiveColorTheme(() => { rebuild(); paintAll(); }),
    vscode.workspace.onDidChangeTextDocument((e) => {
      if (e.document?.languageId === LANGUAGE_ID) later();
    }),
    vscode.workspace.onDidChangeConfiguration((e) => {
      if (!e.affectsConfiguration || e.affectsConfiguration(`${CONFIG_SECTION}.${SETTINGS.colors}`)) {
        rebuild();
        paintAll();
      }
    }),
  );
  return { paintAll, typesFor: () => types };
};

export { DEBOUNCE_MS, rangesFor, register };
