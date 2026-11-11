// core/launch/shareLinks.js: every link starts from the running page, drops its own query and
// fragment, and reads back through the launch grammar as the picture it names.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { projectNameLink, sourceLink, serverProjectLink, projectShareLinks } from '../../../js/core/launch/shareLinks.js';
import { parseLaunchUrl } from '../../../js/core/launch/query.js';

const PAGE = 'https://andrew1407.github.io/stencil/?project=Old#token=x';

test('each link is the page without its own pairs, plus one launch pair', () => {
  assert.equal(projectNameLink(PAGE, 'My Plan'), 'https://andrew1407.github.io/stencil/?project=My+Plan');
  assert.equal(sourceLink(PAGE, 'https://cdn.example/a b.png'),
    'https://andrew1407.github.io/stencil/?source=https%3A%2F%2Fcdn.example%2Fa+b.png');
  assert.equal(serverProjectLink('http://localhost:8080/', 'https://srv.example/', 'p_1'),
    'http://localhost:8080/?project=https%3A%2F%2Fsrv.example%2Fprojects%2Fp_1');
});

test('each link parses back to the picture it was made for', () => {
  assert.deepEqual(parseLaunchUrl(projectNameLink(PAGE, 'Sketch')).picture, { kind: 'project', server: null, ref: 'Sketch' });
  assert.deepEqual(parseLaunchUrl(sourceLink(PAGE, 'https://cdn.example/c.png')).picture,
    { kind: 'source', url: 'https://cdn.example/c.png', frame: null });
  assert.deepEqual(parseLaunchUrl(serverProjectLink(PAGE, 'https://srv.example', 'p_9')).picture,
    { kind: 'project', server: 'https://srv.example', ref: 'p_9' });
});

test('projectShareLinks lists only what the project has, name then source then server', () => {
  const all = projectShareLinks(PAGE, { name: 'A', source: 'https://x.example/a.png', serverUrl: 'https://s.example', remoteId: 'r1' });
  assert.deepEqual(all.map((l) => l.kind), ['name', 'source', 'server']);
  assert.deepEqual(projectShareLinks(PAGE, { name: ' ', source: 'data:image/png;base64,AA' }), []);
  assert.deepEqual(projectShareLinks(PAGE, { name: 'B', serverUrl: 'https://s.example' }).map((l) => l.kind), ['name']);
});
