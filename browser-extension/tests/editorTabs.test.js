// src/lib/editorTabs.js — recognising an editor tab, projecting one into an EditorRow, and
// matching those rows against a query.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { isEditorTab, editorRow, matchEditors } from '../src/lib/editorTabs.js';

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
