// Per-declaration ports: a module that shares only PART of another lists the declarations it
// shares, each matched verbatim (mid-body comments included) against its original, through the
// same declaration() tools/syncTwins.mjs re-copies with. Indent and `export` are placement.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync } from 'node:fs';
import { declaration, declarationShape } from '../../tools/syncTwins.mjs';

const read = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');

// [name, original, copy, declarations]; a wasm-routed original keeps a `JS` suffix.
const FUNCTIONS = [
  ['popover', '../../browser/js/ui/tip/popover.js', '../src/lib/tip/popover.js', ['popoverPosition',
    'DOUBLE_CLICK_MS', 'LONG_PRESS_MS', 'PRESS_SLOP_PX', 'LINGER_CLOSE_MS', 'glideRegistry',
    'createModalOpenGesture']],
  ['cropGeometry', '../../browser/js/core/parse/cropGeometry.js', '../src/lib/image/cropGeometry.js',
    ['isAlbumOrientation', 'cropAspect', 'centeredCrop', 'resizeCropFromCorner',
     'moveCropClamped', 'scaleCropCentered']],
  // motion/ is the extension's own implementation, split along the browser's own file boundaries;
  // only what it shares to the letter with the app is listed.
  ['motion', '../../browser/js/ui/motion/', '../src/lib/motion/', [
    'REVEAL_ITEM_CLASS', 'REVEAL_IN_CLASS', 'REVEAL_ENTERING_CLASS', 'REVEAL_MASKED_CLASS',
    'revealDissolve', 'revealGrain',
    'LEAVING_CLASS', 'createListHold', 'emptyStateVisible',
    'TILE_GATHER_SHARE', 'TILE_JITTER_SHARE', 'tileNoise', 'tileWaypoint', 'reshapeGrid',
    'reintegrate', 'rectCenter',
    'MATERIALIZE_CLASS', 'MATERIALIZE_VEIL_CLASS', 'CHAT_ENTERING_CLASS', 'CHAT_SLIDE_CLASS',
    'SURFACE_FORMING_CLASS', 'SURFACE_LEAVING_CLASS', 'SURFACE_DRIVEN_CLASS',
  ]],
  // plan.js shares the §1 mechanics and then applies the extension's own §8/§11.2 rules, so
  // `validateAsk` and `parseOpPlan` stay out.
  ['planParser', '../../browser/js/llm/plan/parser.js', '../src/llm/op/plan.js',
    ['firstJsonObject', 'askAnswerText']],
  ['typedWords', '../../browser/js/ui/bindings/keys/typedWords.js', '../src/options/secrets/typedWords.js',
    ['LONGEST', 'matchTypedWord', 'typedLetter']],
  ['typingTarget', '../../browser/js/utils/dom.js', '../src/options/secrets/typedWords.js', ['isTypingTarget']],
  ['stageAccents', '../../browser/js/core/settings/accents.js', '../src/lib/logo/accents.js', ['normalizeHex']],
  // The stage's lock and lifecycle; only the bare-window test and the mark's selector are the page's own.
  ['logoStage', '../../browser/js/ui/logo/stage.js', '../src/lib/logo/stage.js', [
    'STAGE_CLASS', 'OPEN_CLASS', 'logoStageOpen', 'SWALLOWED', 'closeLogoStage', 'openLogoStage', 'currentLogoStage']],
  // Unpinned on purpose: pixelIconSvg (an opts bag), stageLook (an observer, not the bus), gearStatusRows
  // (labels passed in), enhanceSelect itself (own face, row match, hover delay), swapGeometry bezierY (function form).
  ['webcoreIcons', '../../browser/js/ui/webcore/icons.js', '../src/lib/webcore/icons.js', ['NAME_RE', 'pixelRects', 'themeInk']],
  ['urlRules', '../../browser/js/net/urlRules.js', '../src/lib/connection/model.js', ['isLoopbackHost', 'normalizeUrl', 'parseInviteUrl']],
  ['nameEditor', '../../browser/js/utils/nameEditor.js', '../src/lib/displayName.js', ['NAME_DISPLAY_CHARS', 'shortName']],
  ['chatTurn', '../../browser/js/llm/chat/turn.js', '../src/llm/chatController.js', ['HISTORY_LIMIT', 'MAX_ATTACHMENTS', 'splitDataUrl', 'replayMessages']],
  ['chatGeometry', '../../browser/js/ui/chat/geometry.js', '../src/lib/chat/statusTip.js', ['gearTipFootText']],
  ['deepLink', '../../browser/js/core/launch/deepLink.js', '../src/lib/menu/openIn.js', ['buildStencilSchemeUrl', 'TELEGRAM_START_LIMIT', 'compressOrigin', 'toBase64', 'encodeTelegramStartPayload', 'buildTelegramLink']],
  ['listPaging', '../../browser/js/net/listPaging.js', '../src/lib/connection/rest.js', ['MAX_LIST_PAGES', 'nextPageCursor']],
  ['chatRowMenu', '../../browser/js/ui/chat/row/chatRowMenuModel.js', '../src/lib/chat/msgMenu.js', ['CHAT_ROW_MENU_JUMP_GAP', 'rowMenuLiftPx', 'rowMenuLiftFits']],
  // enhanceSelect's menu half, over an explicit state object; the face, row match and hover delay stay each side's.
  ['customSelectMenu', '../../browser/js/ui/control/customSelect.js', '../src/lib/control/customSelect.js', ['applySearch', 'buildOptions', 'openMenu', 'closeMenu']],
  // In-extension inline copies: a content script or an injected function cannot import.
  ['pageImagesEditorApi', '../src/lib/image/pageImages.js', '../src/content/editorApiMain.js', ['nameFromUrl']],
  ['pageImagesMedia', '../src/lib/image/pageImages.js', '../src/content/pageApiMedia.js', ['cssImageUrls', 'nameFromUrl']],
  ['pageImagesScan', '../src/lib/image/pageImages.js', '../src/lib/image/scan.js', ['cssImageUrls']],
  ['highlightRgb', '../src/lib/highlight/highlight.js', '../src/lib/highlight/hoverHighlight.js', ['toRgb']],
];

// A port as one string: a file, or every .js under a directory — either side may hold its
// modules in feature folders, so the walk is recursive.
const jsUnder = (dir) => readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))
  .flatMap((e) => (e.isDirectory()
    ? jsUnder(new URL(`${e.name}/`, dir))
    : (e.name.endsWith('.js') ? [readFileSync(new URL(e.name, dir), 'utf8')] : [])));

const sourceOf = (rel) => {
  if (!rel.endsWith('/')) return read(rel);
  return jsUnder(new URL(rel, import.meta.url)).join('\n');
};

for (const [name, browserPath, extPath, fns] of FUNCTIONS) {
  test(`${name}: every ported function matches its browser original`, () => {
    assert.notEqual(browserPath, extPath, `${name}: both columns name the same file`);
    const browser = sourceOf(browserPath);
    const ext = sourceOf(extPath);
    for (const fn of fns) {
      const mine = declarationShape(declaration(ext, fn));
      assert.ok(mine, `${name}: the extension no longer declares ${fn}`);
      const theirs = declarationShape(declaration(browser, `${fn}JS`)?.replace(`${fn}JS`, fn) ?? declaration(browser, fn));
      assert.ok(theirs, `${name}: ${fn} has no original (nor a ${fn}JS reference)`);
      assert.equal(mine, theirs,
        `${name}.${fn} drifted from its browser original — change one, change the other`);
    }
  });
}
