// The address bar naming the open project: withOpenProject (js/core/launch/query.js) builds the URL
// parts, syncProjectUrl (js/ui/projects/window/projectUrl.js) writes them, or leaves the URL alone.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { withOpenProject, parseLaunchQuery } from '../../../js/core/launch/query.js';
import { openProjectRef, syncProjectUrl } from '../../../js/ui/projects/window/projectUrl.js';

test('withOpenProject: a project opened on a bare page joins the query', () => {
  assert.deepEqual(withOpenProject('', '', 'pasted'), { search: '?project=pasted', hash: '' });
  assert.deepEqual(withOpenProject('', '', 'Floor plan'), { search: '?project=Floor+plan', hash: '' });
  assert.equal(parseLaunchQuery('?project=Floor+plan', '').picture.ref, 'Floor plan', 'the link it writes reads back');
});

test('withOpenProject: a rename replaces the name; a close leaves no project', () => {
  assert.deepEqual(withOpenProject('?project=pasted', '', 'Kitchen'), { search: '?project=Kitchen', hash: '' });
  assert.deepEqual(withOpenProject('?project=Kitchen', '', null), { search: '', hash: '' });
  assert.equal(withOpenProject('?project=Kitchen', '', 'Kitchen'), null, 'already named: nothing to write');
  assert.equal(withOpenProject('', '', null), null, 'nothing open on a bare page: nothing to write');
});

test('withOpenProject: the launch pairs a project used up leave, by any spelling', () => {
  const spent = '?src=https%3A%2F%2Fx%2Fa.png&frame=3&layout=https%3A%2F%2Fx%2Fl.json&desc=d&color=red&expires=2+weeks'
    + '&blank=red&width=9&height=9&name=N&keywords=k&incognito&script=%40crop+10%25&open=p_1&server=s&id=1&version=2';
  assert.deepEqual(withOpenProject(spent, '', 'a'), { search: '?project=a', hash: '' });
});

test('withOpenProject: the look, settings, assistant, chat and connections stay', () => {
  const kept = 'theme=dark&accent=teal&line-color=red&llm-provider=ollama&chat=open&connect=http%3A%2F%2Fh%3A8090';
  assert.deepEqual(withOpenProject(`?${kept}&source=https%3A%2F%2Fx%2Fa.png`, '', 'a'),
    { search: `?${kept}&project=a`, hash: '' });
});

test('withOpenProject: a pairs fragment loses its spent pairs too; any other fragment is untouched', () => {
  assert.deepEqual(withOpenProject('', '#source=https%3A%2F%2Fx%2Fa.png&theme=light', 'a'),
    { search: '?project=a', hash: '#theme=light' });
  assert.deepEqual(withOpenProject('', '#source=https%3A%2F%2Fx%2Fa.png', 'a'), { search: '?project=a', hash: '' });
  assert.deepEqual(withOpenProject('', '#section', 'a'), { search: '?project=a', hash: '#section' });
});

test('openProjectRef: a saved project by name, a server-only one by its link, else none', () => {
  const store = { getMeta: (id) => ({ p1: { name: ' Kitchen ' }, p2: { name: '' } })[id] };
  const remoteLink = { address: 'http://h:8090/', remoteId: 'r9' };
  assert.equal(openProjectRef({ activeProjectId: 'p1', storage: { store }, remoteLink }), 'Kitchen', 'the saved name wins');
  assert.equal(openProjectRef({ activeProjectId: null, storage: { store }, remoteLink }), 'http://h:8090/projects/r9');
  assert.equal(openProjectRef({ activeProjectId: 'p2', storage: { store }, imageBaseName: 'pasted' }), 'pasted',
    'a project not yet saved goes by its picture, as the title does');
  assert.equal(openProjectRef({ activeProjectId: 'p2', storage: { store }, remoteLink: null }), null);
  assert.equal(openProjectRef({ activeProjectId: null, storage: { store } }), null);
});

// A window whose URL the writer reads and rewrites; `parent` is itself unless framed.
const fakeWindow = (url, { framed = false } = {}) => {
  const u = new URL(url);
  const writes = [];
  const win = {
    location: { get pathname() { return u.pathname; }, get search() { return u.search; }, get hash() { return u.hash; } },
    history: { state: null, replaceState: (_s, _t, to) => { writes.push(to); const n = new URL(to, u); u.search = n.search; u.hash = n.hash; } },
    writes, get href() { return u.href; },
  };
  win.parent = framed ? {} : win;
  return win;
};
const appOpen = (name, extra = {}) => ({
  activeProjectId: name ? 'p1' : null, launchQuery: null, storage: { incognito: false, store: { getMeta: () => ({ name }) } }, ...extra,
});

test('syncProjectUrl: follows paste, rename and close, writing only on a change', () => {
  const win = fakeWindow('http://localhost:8080/?theme=dark');
  assert.equal(syncProjectUrl(appOpen('pasted'), win), true);
  assert.equal(win.href, 'http://localhost:8080/?theme=dark&project=pasted');
  assert.equal(syncProjectUrl(appOpen('pasted'), win), false, 'the same project writes nothing');
  syncProjectUrl(appOpen('Kitchen'), win);
  assert.equal(win.href, 'http://localhost:8080/?theme=dark&project=Kitchen');
  syncProjectUrl(appOpen(null), win);
  assert.equal(win.href, 'http://localhost:8080/?theme=dark');
  assert.equal(win.writes.length, 3);
});

test('syncProjectUrl: incognito, a frame, a URL not yet read, a pending launch or hand-off leave the URL alone', () => {
  const cases = [
    [fakeWindow('http://h/?incognito'), appOpen('a', { storage: { incognito: true, store: { getMeta: () => ({ name: 'a' }) } } })],
    [fakeWindow('http://h/', { framed: true }), appOpen('a')],
    [fakeWindow('http://h/?source=x'), appOpen('a', { launchQuery: {} })],
    [fakeWindow('http://h/?project=Kitchen'), appOpen(null, { launchQuery: undefined })],
    [fakeWindow('http://h/?blank=red&incognito'), appOpen(null, { launchApplying: true })],
    [fakeWindow('http://h/#stencil=%7B%7D'), appOpen('a')],
    [fakeWindow('http://h/#stencil-await'), appOpen('a')],
  ];
  for (const [win, app] of cases) {
    const before = win.href;
    assert.equal(syncProjectUrl(app, win), false, before);
    assert.equal(win.href, before);
    assert.equal(win.writes.length, 0);
  }
  assert.equal(syncProjectUrl(appOpen('a'), undefined), false, 'no window: nothing to write');
});
