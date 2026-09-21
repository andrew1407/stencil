// The source-page picker half of src/lib/editorTabs.js: which tabs are scannable, how each is
// labelled and filtered, and the import mode an editor's own state earns.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { editorRow, importModeFor, matchSourceTabs, sourceTabChoices } from '../../../src/lib/menu/editorTabs.js';

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
