// applyLaunchQuery (js/console/launchQuery.js) over the facade rig — the look, the picture, the
// layout and the project fields: every option lands on the facade call a typed line would make,
// the URL is stripped at once, and every problem is one notice, never a rejection. The servers,
// the assistant and the chat are launchQueryServers.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  boot, decodeTo, renames, notices, replaced, texts, installFetchStub, called, lastCall, blankCanvases,
  applyLaunchQuery, parseLaunchQuery,
} from '../helpers/launchQueryRig.js';

test('a settings-only query sets each key through the facade, strips the URL and leaves no notice', async () => {
  const { app, stencil } = boot('?theme=auto&main-color=yellow&line-color=%23ff0000&thickness=3');
  await applyLaunchQuery({ app, stencil });
  assert.deepEqual(lastCall(app, 'setThemeMode'), ['setThemeMode', 'system']);
  assert.deepEqual(lastCall(app, 'setAccent'), ['setAccent', 'yellow']);
  assert.deepEqual(lastCall(app, 'setColor'), ['setColor', '#ff0000']);
  assert.deepEqual(lastCall(app, 'setThickness'), ['setThickness', 3]);
  assert.deepEqual(replaced, [[null, '', '/app']]);
  assert.equal(app.launchQuery, null, 'consumed once');
  assert.deepEqual(notices, []);
});

test('no query: nothing is rewritten and nothing is said', async () => {
  const { app, stencil } = boot('');
  assert.equal(app.launchQuery, null);
  await applyLaunchQuery({ app, stencil });
  assert.deepEqual(replaced, []);
  assert.deepEqual(notices, []);
});

test('an explicit query (a console /open) applies without touching the URL', async () => {
  const { app, stencil } = boot('');
  await applyLaunchQuery({ app, stencil, query: parseLaunchQuery('?thickness=7') });
  assert.deepEqual(lastCall(app, 'setThickness'), ['setThickness', 7]);
  assert.deepEqual(replaced, []);
});

test('an unknown option, a bad value and a query-borne secret are each one notice; the rest still applies', async () => {
  const { app, stencil } = boot('?utm_source=x&main-color=notacolor&llm-api-key=k&thickness=2');
  await applyLaunchQuery({ app, stencil });
  assert.deepEqual(lastCall(app, 'setThickness'), ['setThickness', 2]);
  assert.equal(notices.length, 3);
  assert.ok(texts().every((m) => m.startsWith('Launch link: ')));
  assert.ok(texts().some((m) => m.includes('"llm-api-key" is ignored in the ?query')));
  assert.ok(texts().some((m) => m.includes('mainTheme — Unknown theme')));
  assert.ok(texts().some((m) => m.includes('unknown option utmSource')));
  assert.ok(notices.every(([, type]) => type === 'info'));
});

test('the fragment carries pairs too, and a consumed fragment is stripped with the query', async () => {
  const { app, stencil } = boot('?theme=dark', '#main-color=sky');
  await applyLaunchQuery({ app, stencil });
  assert.deepEqual(lastCall(app, 'setThemeMode'), ['setThemeMode', 'dark']);
  assert.deepEqual(lastCall(app, 'setAccent'), ['setAccent', 'sky']);
  assert.deepEqual(replaced, [[null, '', '/app']]);
});

test('a fragment that is not this grammar stays on the URL', async () => {
  const { app, stencil } = boot('?theme=dark', '#about');
  await applyLaunchQuery({ app, stencil });
  assert.deepEqual(replaced, [[null, '', '/app#about']]);
});

test('incognito alone opens an empty incognito editor', async () => {
  const { app, stencil } = boot('?incognito');
  await applyLaunchQuery({ app, stencil });
  assert.equal(app.storage.incognito, true);
  assert.equal(called(app, 'updateIncognitoUI').length, 1);
  assert.deepEqual(notices, []);
});

test('blank= paints the colour through stencil.blank, and name= renames the project it made', async () => {
  const { app, stencil } = boot('?blank=%23336699&width=90&height=60&name=Sketch', '', renames);
  const decode = decodeTo(app);
  try { await applyLaunchQuery({ app, stencil }); } finally { decode.restore(); }
  assert.equal(blankCanvases.at(-1).fills[0][0], '#336699');
  assert.match(decode.reads()[0], /^blank-\d+x\d+\.png$/);
  assert.deepEqual(lastCall(app, 'renameProject'), ['renameProject', 'p1', 'Sketch']);
  assert.equal(app.imageBaseName, 'Sketch');
  assert.deepEqual(notices, []);
});

test('an incognito blank is never saved: the name stays on the picture and the session is reported', async () => {
  const { app, stencil } = boot('?blank=red&incognito&name=Draft', '', renames);
  const decode = decodeTo(app);
  try { await applyLaunchQuery({ app, stencil }); } finally { decode.restore(); }
  assert.equal(app.storage.incognito, true);
  assert.equal(app.activeProjectId, null);
  assert.equal(called(app, 'renameProject').length, 0);
  assert.equal(app.imageBaseName, 'Draft');
  assert.ok(called(app, 'reportIncognito').length >= 1);
  assert.deepEqual(notices, []);
});

test('source= goes through stencil.load, keeps the URL as the project source, and name= renames what it made', async () => {
  const { app, stencil } = boot('?source=https://pics.example/cat.png&name=Kitty', '', { ...renames, history: { reset() {} } });
  const fetchStub = installFetchStub({ blob: new Blob(['x'], { type: 'image/png' }) });
  const decode = decodeTo(app, { settle: true });
  try { await applyLaunchQuery({ app, stencil }); } finally { fetchStub.restore(); decode.restore(); }
  assert.equal(fetchStub.calls[0].url, 'https://pics.example/cat.png');
  assert.deepEqual(decode.reads(), ['cat.png']);
  assert.equal(app.imageSource, 'https://pics.example/cat.png', 'the URL is the saved project\'s source');
  assert.deepEqual(lastCall(app, 'renameProject'), ['renameProject', 'p1', 'Kitty']);
  assert.equal(app.storage.incognito, false);
  // The settle's own notice about the rig's missing view is not the link's.
  assert.deepEqual(texts().filter((m) => m.startsWith('Launch link:')), []);
});

test('a picture that cannot be fetched is one notice and never a rejection; earlier settings stay', async () => {
  const { app, stencil } = boot('?source=https://pics.example/cat.png&thickness=5');
  const fetchStub = installFetchStub(new Error('offline'));
  try { await applyLaunchQuery({ app, stencil }); } finally { fetchStub.restore(); }
  assert.deepEqual(lastCall(app, 'setThickness'), ['setThickness', 5]);
  assert.equal(app.image, null);
  assert.deepEqual(texts(), ['Launch link: picture — offline']);
});

test('layout= is fetched through the guard and installed; layout-raw= needs no fetch', async () => {
  const layout = { imageWidth: 4, imageHeight: 3, lines: [] };
  const bytes = new TextEncoder().encode(JSON.stringify(layout)).buffer;
  const fetched = boot('?blank=white&layout=https://cdn.example/l.json');
  const fetchStub = installFetchStub(() => Promise.resolve({ ok: true, status: 200, arrayBuffer: async () => bytes }));
  const decode = decodeTo(fetched.app);
  try { await applyLaunchQuery(fetched); } finally { fetchStub.restore(); decode.restore(); }
  assert.equal(fetchStub.calls[0].url, 'https://cdn.example/l.json');
  assert.deepEqual(lastCall(fetched.app, 'installLayout')[1], layout);
  assert.deepEqual(notices, []);

  const inline = boot(`?blank=white&layout-raw=${encodeURIComponent(JSON.stringify(layout))}`);
  const noFetch = installFetchStub();
  const decode2 = decodeTo(inline.app);
  try { await applyLaunchQuery(inline); } finally { noFetch.restore(); decode2.restore(); }
  assert.equal(noFetch.calls.length, 0);
  assert.deepEqual(lastCall(inline.app, 'installLayout')[1], layout);
});

test('a layout with no picture to put it on is one notice', async () => {
  const { app, stencil } = boot('?layout=https://cdn.example/l.json');
  const noFetch = installFetchStub();
  try { await applyLaunchQuery({ app, stencil }); } finally { noFetch.restore(); }
  assert.equal(noFetch.calls.length, 0);
  assert.deepEqual(texts(), ['Launch link: layout needs a picture — add source= or blank=']);
});

test('description, keywords, colour and expiry land on the saved project; a duration and a date both set the expiry', async () => {
  const { app, stencil } = boot('?blank=white&desc=Hello&keywords=a,%20b&color=%23ff0000&expire=2%20weeks');
  const decode = decodeTo(app);
  const before = Date.now();
  try { await applyLaunchQuery({ app, stencil }); } finally { decode.restore(); }
  assert.deepEqual(lastCall(app, 'setProjectDescription'), ['setProjectDescription', 'p1', 'Hello']);
  assert.deepEqual(lastCall(app, 'setProjectKeywords'), ['setProjectKeywords', 'p1', ['a', 'b']]);
  assert.deepEqual(lastCall(app, 'setProjectColor'), ['setProjectColor', 'p1', '#ff0000']);
  const twoWeeks = 14 * 24 * 60 * 60 * 1000;
  const { expiresAt } = lastCall(app, 'setProjectExpiration')[2];
  assert.ok(expiresAt >= before + twoWeeks && expiresAt <= Date.now() + twoWeeks);
  assert.deepEqual(notices, []);

  const dated = boot('?blank=white&expire=2099-01-02');
  const decode2 = decodeTo(dated.app);
  try { await applyLaunchQuery(dated); } finally { decode2.restore(); }
  assert.deepEqual(lastCall(dated.app, 'setProjectExpiration'),
    ['setProjectExpiration', 'p1', { expiresAt: new Date('2099-01-02').getTime() }]);
});

test('on a server project the fields stay local and the expiry is ignored, with one notice', async () => {
  const local = (name) => function (id, v, opts) { this.calls.push([name, id, v, opts]); return {}; };
  const { app, stencil } = boot('?blank=white&desc=Hello&keywords=a&color=%23ff0000&expire=1%20day', '', {
    remoteLink: { address: 'https://team.example', remoteId: 'r1', version: 3 },
    setProjectDescription: local('setProjectDescription'), setProjectKeywords: local('setProjectKeywords'),
    setProjectColor: local('setProjectColor'),
  });
  const decode = decodeTo(app);
  try { await applyLaunchQuery({ app, stencil }); } finally { decode.restore(); }
  assert.deepEqual(lastCall(app, 'setProjectDescription'), ['setProjectDescription', 'p1', 'Hello', { local: true }]);
  assert.deepEqual(lastCall(app, 'setProjectKeywords'), ['setProjectKeywords', 'p1', ['a'], { local: true }]);
  assert.deepEqual(lastCall(app, 'setProjectColor'), ['setProjectColor', 'p1', '#ff0000', { local: true }]);
  assert.equal(called(app, 'setProjectExpiration').length, 0, 'a link never expires a shared project');
  assert.deepEqual(texts(), ['Launch link: expire — ignored — a link never expires a server project']);
});

test('project fields on an incognito editor are one notice, not a crash', async () => {
  const { app, stencil } = boot('?blank=white&incognito&desc=Hello');
  const decode = decodeTo(app);
  try { await applyLaunchQuery({ app, stencil }); } finally { decode.restore(); }
  assert.equal(called(app, 'setProjectDescription').length, 0);
  assert.equal(notices.length, 1);
  assert.match(texts()[0], /need a saved project/);
});

test('a script waits in pendingLaunchScript unless the fragment hand-off already left one', async () => {
  const a = boot('?script=%40crop%2010%25');
  await applyLaunchQuery(a);
  assert.equal(a.app.pendingLaunchScript, '@crop 10%');
  const b = boot('?script=%40crop%2010%25');
  b.app.pendingLaunchScript = '@filter bw';
  await applyLaunchQuery(b);
  assert.equal(b.app.pendingLaunchScript, '@filter bw');
});
