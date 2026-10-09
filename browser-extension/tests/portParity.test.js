// The extension can't import across subprojects, so a few browser modules are duplicated
// into src/ as rule-for-rule PORTS. Their behavioural cases live in browser/tests/; this
// is the drift guard that lets the extension drop those duplicated suites: whole-file pairs here,
// per-declaration ports in portParityFunctions.test.js. Body differences are never normalized
// away — a rewording in either copy fails the pair.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

// name → [browser copy, extension copy], both relative to this file.
const MANIFEST = [
  // The one HTML escaper both surfaces (and tipContent below) re-export.
  ['escapeHtml', '../../browser/js/ui/escapeHtml.js', '../src/lib/escapeHtml.js'],
  ['tipContent', '../../browser/js/ui/tip/content.js', '../src/lib/tip/content.js'],
  // tipContent's keys half: the key vocabulary and the keycaps it draws.
  ['tipKeys', '../../browser/js/ui/tip/keys.js', '../src/lib/tip/keys.js'],
  ['numericInput', '../../browser/js/ui/control/numericInput.js', '../src/lib/control/numericInput.js'],
  // The numeric field's evaluator, pure and DOM-free, so the copy is the whole file.
  ['numericExpr', '../../browser/js/ui/control/numericExpr.js', '../src/lib/control/numericExpr.js'],
  ['dropdownMenu', '../../browser/js/ui/control/dropdownMenu.js', '../src/lib/control/dropdownMenu.js'],
  ['controlTooltip', '../../browser/js/ui/tip/controlTooltip.js', '../src/lib/tip/controlTooltip.js'],
  // The hold a control drag puts on every tooltip, which the control tip asks: pure module state.
  ['tipHold', '../../browser/js/ui/tip/tipHold.js', '../src/lib/tip/tipHold.js'],
  // A written shortcut against a keystroke: pure, so the copy is the whole file.
  ['comboMatch', '../../browser/js/ui/control/comboMatch.js', '../src/lib/control/comboMatch.js'],
  ['scrollbarHover', '../../browser/js/ui/control/scrollbarHover.js', '../src/lib/control/scrollbarHover.js'],
  // A thumb's arithmetic and the menu bar it draws: pure, so each copy is the whole file.
  ['thumbMetrics', '../../browser/js/ui/control/thumbMetrics.js', '../src/lib/control/thumbMetrics.js'],
  ['menuScrollbar', '../../browser/js/ui/control/menuScrollbar.js', '../src/lib/control/menuScrollbar.js'],
  // The cloud's two halves: the flight table a grain is posed by, and the shape it wears.
  ['dustFlight', '../../browser/js/ui/dust/flight.js', '../src/lib/dust/flight.js'],
  ['dustGrain', '../../browser/js/ui/dust/grain.js', '../src/lib/dust/grain.js'],
  // The one-canvas dust cloud every element-sized flight rides: pure flight table +
  // painter, so the copy is the whole file.
  ['dustCloud', '../../browser/js/ui/dust/cloud.js', '../src/lib/dust/cloud.js'],
  // The motion modes' glyphs: pure SVG strings, so the copy is the whole file.
  ['motionIcons', '../../browser/js/ui/motion/icons.js', '../src/lib/motionIcons.js'],
  // The crop rect's flight between two shapes: a pure rAF ramp, so the copy is the whole file.
  ['rectTween', '../../browser/js/ui/motion/rectTween.js', '../src/lib/rectTween.js'],
  // The picture's quarter turn: one WAAPI flight on the clock its caller hands it.
  ['quarterTurn', '../../browser/js/ui/motion/quarterTurn.js', '../src/lib/motion/quarterTurn.js'],
  // The shared LLM client: per-surface wording/token defaults live in surface.js,
  // so the client itself differs only in its header + providers.json import path.
  ['llmClient', '../../browser/js/llm/client.js', '../src/llm/client.js'],
  // The typed LlmError and the one JSON POST every provider goes through.
  ['llmHttp', '../../browser/js/llm/http.js', '../src/llm/http.js'],
  // The capped body read a server's and a provider's reply go through: pure, the whole file.
  ['cappedBody', '../../browser/js/net/cappedBody.js', '../src/lib/connection/cappedBody.js'],
  // The registry-driven validation engine: pure, registry-in/verdict-out, so the copy
  // is the whole file.
  ['opSchema', '../../browser/js/llm/plan/opSchema.js', '../src/llm/op/schema.js'],
  // Its closure-free base: predicates, the SchemaError, message paths, the native rules.
  ['opSchemaBase', '../../browser/js/llm/plan/opSchemaBase.js', '../src/llm/op/opSchemaBase.js'],
  // The §1 JSON caps over a parsed plan (E_JSON_LIMIT): pure, so the copy is the whole file.
  ['planCaps', '../../browser/js/llm/plan/planCaps.js', '../src/llm/op/planCaps.js'],
  // The un-persisted "Swap message sides" preference: pure module state, so the copy is
  // the whole file.
  ['chatLayoutPrefs', '../../browser/js/ui/chat/layoutPrefs.js', '../src/lib/chat/layoutPrefs.js'],
  // The logo shows' pure half: the table's rules, the kinematics, the cloud, the painter, the
  // pointer memory and the notice's golden shining.
  ['stageRules', '../../browser/js/ui/logo/stageRules.js', '../src/lib/logo/stageRules.js'],
  ['stageMotion', '../../browser/js/ui/logo/stageMotion.js', '../src/lib/logo/stageMotion.js'],
  ['stageCloud', '../../browser/js/ui/logo/stageCloud.js', '../src/lib/logo/stageCloud.js'],
  ['stagePaint', '../../browser/js/ui/logo/stagePaint.js', '../src/lib/logo/stagePaint.js'],
  ['logoPointer', '../../browser/js/ui/logo/pointer.js', '../src/lib/logo/pointer.js'],
  ['toastGlow', '../../browser/js/ui/dust/toastGlow.js', '../src/lib/logo/toastGlow.js'],
  // A dropdown's Alt-hover peek, riding the mini window's gesture machine below.
  ['altPeek', '../../browser/js/ui/tip/altPeek.js', '../src/lib/tip/altPeek.js'],
  // The typed boundaries the extension copies unchanged from the browser's own .d.ts.
  ['dblResetDts', '../../browser/js/ui/control/dblReset.d.ts', '../src/lib/control/dblReset.d.ts'],
  ['thumbMetricsDts', '../../browser/js/ui/control/thumbMetrics.d.ts', '../src/lib/control/thumbMetrics.d.ts'],
  ['menuScrollbarDts', '../../browser/js/ui/control/menuScrollbar.d.ts', '../src/lib/control/menuScrollbar.d.ts'],
  ['dustFlightDts', '../../browser/js/ui/dust/flight.d.ts', '../src/lib/dust/flight.d.ts'],
  ['dustGrainDts', '../../browser/js/ui/dust/grain.d.ts', '../src/lib/dust/grain.d.ts'],
  ['rectTweenDts', '../../browser/js/ui/motion/rectTween.d.ts', '../src/lib/rectTween.d.ts'],
  ['quarterTurnDts', '../../browser/js/ui/motion/quarterTurn.d.ts', '../src/lib/motion/quarterTurn.d.ts'],
  ['cappedBodyDts', '../../browser/js/net/cappedBody.d.ts', '../src/lib/connection/cappedBody.d.ts'],
  ['stageRulesDts', '../../browser/js/ui/logo/stageRules.d.ts', '../src/lib/logo/stageRules.d.ts'],
  ['stageMotionDts', '../../browser/js/ui/logo/stageMotion.d.ts', '../src/lib/logo/stageMotion.d.ts'],
  ['stageCloudDts', '../../browser/js/ui/logo/stageCloud.d.ts', '../src/lib/logo/stageCloud.d.ts'],
  ['logoPointerDts', '../../browser/js/ui/logo/pointer.d.ts', '../src/lib/logo/pointer.d.ts'],
  ['toastGlowDts', '../../browser/js/ui/dust/toastGlow.d.ts', '../src/lib/logo/toastGlow.d.ts'],
  ['altPeekDts', '../../browser/js/ui/tip/altPeek.d.ts', '../src/lib/tip/altPeek.d.ts'],
  ['planCapsDts', '../../browser/js/llm/plan/planCaps.d.ts', '../src/llm/op/planCaps.d.ts'],
  ['escapeHtmlDts', '../../browser/js/ui/escapeHtml.d.ts', '../src/lib/escapeHtml.d.ts'],
  ['dustCloudDts', '../../browser/js/ui/dust/cloud.d.ts', '../src/lib/dust/cloud.d.ts'],
  ['comboMatchDts', '../../browser/js/ui/control/comboMatch.d.ts', '../src/lib/control/comboMatch.d.ts'],
  ['numericExprDts', '../../browser/js/ui/control/numericExpr.d.ts', '../src/lib/control/numericExpr.d.ts'],
  ['tipKeysDts', '../../browser/js/ui/tip/keys.d.ts', '../src/lib/tip/keys.d.ts'],
  ['tipHoldDts', '../../browser/js/ui/tip/tipHold.d.ts', '../src/lib/tip/tipHold.d.ts'],
  ['llmHttpDts', '../../browser/js/llm/http.d.ts', '../src/llm/http.d.ts'],
  ['opSchemaBaseDts', '../../browser/js/llm/plan/opSchemaBase.d.ts', '../src/llm/op/opSchemaBase.d.ts'],
];

const read = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');

// Drop the header: the leading run of `//` lines (and blanks) before the first line of
// code. Comments in the body are NOT touched — a divergence there must fail.
const stripHeader = (src) => {
  const lines = src.split('\n');
  let i = 0;
  while (i < lines.length && (/^\s*\/\//.test(lines[i]) || lines[i].trim() === '')) i++;
  return lines.slice(i).join('\n');
};

// Reduce import specifiers to their basename, so '../src/lib/tip/popover.js' compares equal from
// either directory layout.
const normalizeImports = (src) =>
  src.replace(/(from\s+['"])([^'"]+)(['"])/g, (_, pre, spec, post) => pre + spec.split('/').pop() + post);

const normalize = (src) => normalizeImports(stripHeader(src));

for (const [name, browserPath, extPath] of MANIFEST) {
  test(`${name}: the extension port matches the browser module rule-for-rule`, () => {
    // A row whose two columns name one file compares it to itself and proves nothing.
    assert.notEqual(browserPath, extPath, `${name}: both columns name the same file`);
    const browser = normalize(read(browserPath));
    const ext = normalize(read(extPath));
    assert.ok(browser.trim() && ext.trim(), 'both copies have a body');
    assert.equal(ext, browser,
      `${name} drifted from its browser original — change one, change the other `
      + '(only the header comment and import paths may differ)');
  });
}
