// Unit tests for src/lib/editorTabs.js — the pure tab helpers behind "editor mode" (the
// panel + stencil.extension surface that appears when the active tab IS the Stencil editor).
// Everything here is chrome-free and DOM-free, so it runs under plain `node --test`.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { isEditorTab, editorRow, matchEditors, matchSourceTabs, sourceTabChoices, importModeFor } from '../src/lib/editorTabs.js';
import { readFileSync } from 'node:fs';
import { createFilterTransition } from '../src/lib/motion.js';
import { makeList, renderKeys } from './helpers/listDom.js';

test('isEditorTab: origin match, ignoring path/query/fragment on either side', () => {
  const editorUrl = 'http://localhost:8080/';
  assert.equal(isEditorTab('http://localhost:8080/', editorUrl), true);
  assert.equal(isEditorTab('http://localhost:8080/#stencil=%7B%7D', editorUrl), true);
  assert.equal(isEditorTab('http://localhost:8080/index.html?x=1', editorUrl), true);
  assert.equal(isEditorTab('http://example.com/', editorUrl), false);
});

test('isEditorTab: the port and scheme are part of the origin', () => {
  assert.equal(isEditorTab('http://localhost:8081/', 'http://localhost:8080/'), false);
  assert.equal(isEditorTab('https://localhost:8080/', 'http://localhost:8080/'), false);
  // A configured editor URL with a PATH still matches any page on that origin — the editor
  // may be served under a sub-path, and all of its projects live on the one origin.
  assert.equal(isEditorTab('https://apps.example.com/other', 'https://apps.example.com/stencil/index.html'), true);
  assert.equal(isEditorTab('https://apps.example.com:8443/stencil/', 'https://apps.example.com/stencil/'), false);
});

test('isEditorTab: non-http(s) and unparseable URLs are never the editor', () => {
  assert.equal(isEditorTab('file:///tmp/index.html', 'file:///tmp/index.html'), false);
  assert.equal(isEditorTab('chrome://extensions', 'http://localhost:8080/'), false);
  assert.equal(isEditorTab('about:blank', 'http://localhost:8080/'), false);
  assert.equal(isEditorTab('', 'http://localhost:8080/'), false);
  assert.equal(isEditorTab('http://localhost:8080/', ''), false);
  assert.equal(isEditorTab('not a url', 'not a url'), false);
});

test('editorRow: joins a tab with its bridge state and normalises every field', () => {
  const tab = { id: 7, windowId: 2, url: 'http://localhost:8080/', title: 'Stencil', active: true };
  const state = {
    projectId: 'p1', projectName: 'Sunset', hasImage: true, imageName: 'sunset.png',
    imageSize: { w: 1920, h: 1080 }, incognito: false, thumbnail: 'data:image/png;base64,AAA',
    projects: [{ id: 'p1', name: 'Sunset', active: true }, { id: 'p2', name: 'Draft' }],
  };
  assert.deepEqual(editorRow(tab, state, { currentTabId: 7 }), {
    tabId: 7, windowId: 2, url: 'http://localhost:8080/', title: 'Stencil', active: true,
    current: true, ready: true, projectId: 'p1', projectName: 'Sunset', hasImage: true,
    imageName: 'sunset.png', imageSize: { w: 1920, h: 1080 }, incognito: false,
    thumbnail: 'data:image/png;base64,AAA',
    projects: [{ id: 'p1', name: 'Sunset', active: true }, { id: 'p2', name: 'Draft', active: false }],
  });
});

test('editorRow: another tab is not `current`; a missing currentTabId marks none', () => {
  const tab = { id: 7, url: 'http://localhost:8080/', title: 'Stencil' };
  assert.equal(editorRow(tab, null, { currentTabId: 9 }).current, false);
  assert.equal(editorRow(tab, null).current, false);
});

test('editorRow: no state (bridge silent) still lists the tab, with ready:false', () => {
  const row = editorRow({ id: 3, windowId: 1, url: 'http://localhost:8080/x', title: 'Stencil' }, null);
  assert.equal(row.ready, false);
  assert.equal(row.tabId, 3);
  assert.equal(row.title, 'Stencil');
  assert.equal(row.projectName, '');
  assert.equal(row.hasImage, false);
  assert.equal(row.thumbnail, '');
  assert.equal(row.imageSize, null);
  assert.deepEqual(row.projects, []);
});

test('editorRow: a blank incognito editor — flagged, with no image fields', () => {
  const state = { projectId: 'p9', projectName: 'Untitled', hasImage: false, incognito: true, projects: [] };
  const row = editorRow({ id: 4, url: 'http://localhost:8080/', title: '' }, state);
  assert.equal(row.ready, true);      // it answered — it just has nothing loaded
  assert.equal(row.incognito, true);
  assert.equal(row.hasImage, false);
  assert.equal(row.imageName, '');
  assert.equal(row.imageSize, null);
  assert.equal(row.title, '');
});

test('editorRow: junk state fields fall back rather than reaching the UI', () => {
  const row = editorRow({}, { imageSize: { w: 0, h: 0 }, projects: [{ name: 'no id' }, null], thumbnail: null });
  assert.equal(row.tabId, null);
  assert.equal(row.windowId, null);
  assert.equal(row.url, '');
  assert.equal(row.imageSize, null);   // a zero-sized image is "unknown", not 0x0
  assert.deepEqual(row.projects, []);  // entries without an id can't be switched to
  assert.equal(row.thumbnail, '');
});

test('matchEditors: empty query matches every row', () => {
  const rows = [
    editorRow({ id: 1, url: 'http://localhost:8080/', title: 'Stencil' }, { projectName: 'Sunset' }),
    editorRow({ id: 2, url: 'http://localhost:8080/b', title: 'Stencil' }, null),
  ];
  assert.equal(matchEditors(rows, '').length, 2);
  assert.equal(matchEditors(rows, undefined).length, 2);
  assert.deepEqual(matchEditors([], 'x'), []);
  assert.deepEqual(matchEditors(null, 'x'), []);
});

test('matchEditors: matches project name, tab title or URL, case-insensitively', () => {
  const rows = [
    editorRow({ id: 1, url: 'http://localhost:8080/a', title: 'Stencil — Sunset' }, { projectName: 'Sunset' }),
    editorRow({ id: 2, url: 'http://localhost:9000/b', title: 'Editor' }, { projectName: 'Blueprint' }),
  ];
  assert.deepEqual(matchEditors(rows, 'sunset').map(r => r.tabId), [1]);   // project name
  assert.deepEqual(matchEditors(rows, 'EDITOR').map(r => r.tabId), [2]);   // tab title
  assert.deepEqual(matchEditors(rows, ':9000').map(r => r.tabId), [2]);    // URL
  assert.deepEqual(matchEditors(rows, 'nope').map(r => r.tabId), []);
});

test('matchEditors: regex mode, with an invalid pattern matching nothing', () => {
  const rows = [
    editorRow({ id: 1, url: 'http://localhost:8080/a', title: 'A' }, { projectName: 'Sunset 2' }),
    editorRow({ id: 2, url: 'http://localhost:8080/b', title: 'B' }, { projectName: 'Draft' }),
  ];
  assert.deepEqual(matchEditors(rows, 'sun.*\\d', { regex: true }).map(r => r.tabId), [1]);
  assert.deepEqual(matchEditors(rows, '[', { regex: true }).map(r => r.tabId), []);
});

test('sourceTabChoices: keeps scannable http(s) pages, labelled "title — host"', () => {
  const tabs = [
    { id: 1, url: 'https://news.example.com/story?id=3', title: 'Big story', favIconUrl: 'https://news.example.com/f.ico' },
    { id: 2, url: 'http://shop.example.org:3000/', title: 'Shop' },
  ];
  assert.deepEqual(sourceTabChoices(tabs, { editorUrl: 'http://localhost:8080/' }), [
    { tabId: 1, title: 'Big story', url: 'https://news.example.com/story?id=3', host: 'news.example.com', favIconUrl: 'https://news.example.com/f.ico', label: 'Big story — news.example.com' },
    // A tab with no favicon reports '' rather than undefined, so the picker's <img> is only
    // ever given a real URL or left blank.
    { tabId: 2, title: 'Shop', url: 'http://shop.example.org:3000/', host: 'shop.example.org:3000', favIconUrl: '', label: 'Shop — shop.example.org:3000' },
  ]);
});

test('sourceTabChoices: a non-string favicon is normalised away, never passed on', () => {
  const tabs = [{ id: 1, url: 'https://a.example/', title: 'A', favIconUrl: { evil: true } }];
  assert.equal(sourceTabChoices(tabs, { editorUrl: 'http://localhost:8080/' })[0].favIconUrl, '');
});

test('sourceTabChoices: with editorTabIds, only those tabs are excluded — a same-origin ordinary page stays', () => {
  const tabs = [
    { id: 1, url: 'http://localhost:8080/', title: 'Stencil' },            // the real editor
    { id: 2, url: 'http://localhost:8080/docs/guide.html', title: 'Docs' },  // same origin, NOT an editor
    { id: 3, url: 'https://example.com/', title: 'Page' },
  ];
  const opts = { editorUrl: 'http://localhost:8080/', editorTabIds: [1] };
  assert.deepEqual(sourceTabChoices(tabs, opts).map(t => t.tabId), [2, 3]);
  // Without the authoritative set the cheap origin rule still applies (both localhost tabs go).
  assert.deepEqual(sourceTabChoices(tabs, { editorUrl: 'http://localhost:8080/' }).map(t => t.tabId), [3]);
});

test('matchSourceTabs: filters by URL, substring by default', () => {
  const choices = [
    { tabId: 1, url: 'https://github.com/andrew1407/stencil/blob/main/cat.png' },
    { tabId: 2, url: 'https://github.com/andrew1407/stencil/blob/main/rabbit.png' },
    { tabId: 3, url: 'https://news.example.com/cats-are-great' },
  ];
  assert.deepEqual(matchSourceTabs(choices, 'github').map(c => c.tabId), [1, 2]);
  assert.deepEqual(matchSourceTabs(choices, 'CAT').map(c => c.tabId), [1, 3]);   // case-insensitive
  assert.deepEqual(matchSourceTabs(choices, '').map(c => c.tabId), [1, 2, 3]);   // empty = everything
  assert.deepEqual(matchSourceTabs(choices, '   ').map(c => c.tabId), [1, 2, 3]);
  assert.deepEqual(matchSourceTabs(undefined, 'x'), []);
});

test('matchSourceTabs: regex mode, and an invalid pattern matches nothing', () => {
  const choices = [
    { tabId: 1, url: 'https://github.com/andrew1407/stencil/blob/main/cat.png' },
    { tabId: 2, url: 'https://github.com/andrew1407/stencil/blob/main/rabbit.png' },
    { tabId: 3, url: 'https://news.example.com/cats' },
  ];
  assert.deepEqual(matchSourceTabs(choices, 'main/(cat|rabbit)\\.png$', { regex: true }).map(c => c.tabId), [1, 2]);
  assert.deepEqual(matchSourceTabs(choices, '^https://news', { regex: true }).map(c => c.tabId), [3]);
  // A half-typed pattern must not throw out of the picker's keystroke handler.
  assert.deepEqual(matchSourceTabs(choices, '([unclosed', { regex: true }), []);
  // The same text WITHOUT the pill is a plain substring, so it simply finds nothing here.
  assert.deepEqual(matchSourceTabs(choices, '([unclosed'), []);
});

test('sourceTabChoices: drops blocked schemes, non-http pages, and tabs with no id/url', () => {
  const tabs = [
    { id: 1, url: 'chrome://extensions', title: 'Extensions' },
    { id: 2, url: 'chrome-extension://abc/popup.html', title: 'Popup' },
    { id: 3, url: 'about:blank', title: '' },
    { id: 4, url: 'view-source:https://example.com/', title: 'Source' },
    { id: 5, url: 'edge://settings', title: 'Settings' },
    { id: 6, url: 'file:///tmp/page.html', title: 'Local' },
    { id: 7, url: '', title: 'No URL' },
    { url: 'https://example.com/', title: 'No id' },
    { id: 8, url: 'https://ok.example/', title: 'Fine' },
  ];
  assert.deepEqual(sourceTabChoices(tabs, { editorUrl: 'http://localhost:8080/' }).map(t => t.tabId), [8]);
});

test('sourceTabChoices: excludes the editor origin — an editor tab is a destination', () => {
  const tabs = [
    { id: 1, url: 'http://localhost:8080/', title: 'Stencil' },
    { id: 2, url: 'http://localhost:8080/index.html#stencil=%7B%7D', title: 'Stencil' },
    { id: 3, url: 'https://example.com/', title: 'Page' },
  ];
  assert.deepEqual(sourceTabChoices(tabs, { editorUrl: 'http://localhost:8080/app/' }).map(t => t.tabId), [3]);
  // With no editorUrl configured nothing is treated as an editor (still scheme-filtered).
  assert.deepEqual(sourceTabChoices(tabs, {}).map(t => t.tabId), [1, 2, 3]);
  assert.deepEqual(sourceTabChoices(tabs).map(t => t.tabId), [1, 2, 3]);
});

test('sourceTabChoices: label falls back when the tab has no title', () => {
  const [choice] = sourceTabChoices([{ id: 1, url: 'https://example.com/a', title: '  ' }], {});
  assert.equal(choice.title, '');
  assert.equal(choice.label, 'example.com');
});

test('sourceTabChoices: no tabs → no choices', () => {
  assert.deepEqual(sourceTabChoices([], { editorUrl: 'http://localhost:8080/' }), []);
  assert.deepEqual(sourceTabChoices(null), []);
});

test('importModeFor: an editor holding an image asks; anything else imports straight away', () => {
  assert.equal(importModeFor({ hasImage: true, projectName: 'Sunset' }), 'ask');
  assert.equal(importModeFor({ hasImage: true, incognito: true }), 'ask');   // incognito work has no copy on disk
  assert.equal(importModeFor({ hasImage: false, projectName: 'Untitled' }), 'new');
  assert.equal(importModeFor({}), 'new');
  assert.equal(importModeFor(null), 'new');       // bridge never answered → never destroy
  assert.equal(importModeFor(undefined), 'new');
});

test('importModeFor: accepts an editorRow as-is (both carry hasImage)', () => {
  const occupied = editorRow({ id: 1, url: 'http://localhost:8080/' }, { hasImage: true });
  const blank = editorRow({ id: 2, url: 'http://localhost:8080/' }, { hasImage: false });
  assert.equal(importModeFor(occupied), 'ask');
  assert.equal(importModeFor(blank), 'new');
  assert.equal(importModeFor(editorRow({ id: 3, url: 'http://localhost:8080/' }, null)), 'new');
});

// ── Editor mode's two lists animate their filters both ways ─────────────────
// "Open editors" and "Images from another page" are both rebuilt wholesale on every
// keystroke in their search boxes, so what leaves and what arrives is decided by the
// tab ids of the two renders. The transition's mechanics live in tests/motion.test.js.

const EDITORS = [
  editorRow({ id: 1, url: 'http://localhost:8080/', title: 'Sunset' }, { projectName: 'Sunset' }),
  editorRow({ id: 2, url: 'http://localhost:8080/', title: 'Harbour' }, { projectName: 'Harbour' }),
  editorRow({ id: 3, url: 'http://localhost:8080/', title: 'Sunrise' }, { projectName: 'Sunrise' }),
];
const editorKeys = (q) => matchEditors(EDITORS, q).map((r) => String(r.tabId));

test('the editor search leaves and re-enters rows symmetrically', () => {
  const list = makeList();
  const tr = createFilterTransition({ list, reduced: () => false });

  renderKeys(list, tr, editorKeys(''));
  assert.deepEqual(renderKeys(list, tr, editorKeys('sun')), { entered: [], left: ['2'] });
  assert.equal(tr.ghostCount, 1, 'the excluded editor plays out');
  assert.deepEqual(renderKeys(list, tr, editorKeys('sunset')), { entered: [], left: ['3'] });
  assert.deepEqual(renderKeys(list, tr, editorKeys('')), { entered: ['2', '3'], left: [] },
    'clearing the box brings both back as arrivals');
  assert.deepEqual(list.keys(), ['1', '2', '3']);
  assert.equal(tr.ghostCount, 0, 'a burst of typing leaks no half-faded rows');
});

test('the source-page filter animates the same way, and reduced motion just lands', () => {
  const choices = sourceTabChoices([
    { id: 10, url: 'https://example.com/a', title: 'A' },
    { id: 11, url: 'https://other.org/b', title: 'B' },
  ], {});
  const keys = (q) => matchSourceTabs(choices, q).map((c) => String(c.tabId));

  const list = makeList();
  const tr = createFilterTransition({ list, reduced: () => false });
  renderKeys(list, tr, keys(''));
  assert.deepEqual(renderKeys(list, tr, keys('example')), { entered: [], left: ['11'] });

  const still = makeList();
  const quiet = createFilterTransition({ list: still, reduced: () => true });
  renderKeys(still, quiet, keys(''));
  renderKeys(still, quiet, keys('example'));
  assert.deepEqual(still.keys(), ['10'], 'reduced motion: the right set, with no animation');
  assert.equal(quiet.ghostCount, 0);
});

test('editor mode wraps both rebuilds and keys its rows by tab id', () => {
  const js = ['editorList.js', 'sourceTabsList.js'].map((f) => readFileSync(new URL(`../src/popup/${f}`, import.meta.url), 'utf8')).join('\n');
  assert.match(js, /edTransition\.begin\(\);\s*\n\s*listEl\.textContent = '';/);
  assert.match(js, /srcTransition\.begin\(\);\s*\n\s*listedEl\.textContent = '';/);
  assert.match(js, /edTransition\.end\(\)/);
  assert.match(js, /srcTransition\.end\(\)/);
  assert.equal((js.match(/li\.dataset\.key = String\((row|c)\.tabId\)/g) || []).length, 2,
    'both row builders carry the key the transition diffs by');
});
