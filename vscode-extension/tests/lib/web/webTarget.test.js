// The instance URL is explicit user configuration, like the CLI path: the setting, else the
// published default, and never anything else. A URL that is not http(s) names no instance.
import test from 'node:test';
import assert from 'node:assert/strict';

import { installVscodeStub, makeDocument, makeVscode } from '../../helpers/vscodeStub.js';

const PUBLISHED = 'https://andrew1407.github.io/stencil/';

const withHost = async (settings, body) => {
  const { vscode } = makeVscode({ settings });
  const host = installVscodeStub(vscode);
  try {
    return await body({ vscode, webTarget: host.require('lib/web/target.js') });
  } finally {
    host.restore();
  }
};

test('an unset setting means the published instance', async () => {
  await withHost({}, ({ vscode, webTarget }) => {
    assert.equal(webTarget.webUrlFor(vscode), PUBLISHED);
    assert.equal(webTarget.normalizeWebUrl(''), PUBLISHED);
    assert.equal(webTarget.normalizeWebUrl('   '), PUBLISHED);
  });
});

test('a configured instance is taken as given, minus any fragment', async () => {
  await withHost({}, ({ webTarget }) => {
    assert.equal(webTarget.normalizeWebUrl('http://localhost:8080'), 'http://localhost:8080/');
    // A hand-off appends its own `#stencil=`, so an instance may not arrive carrying one.
    assert.equal(webTarget.normalizeWebUrl('https://a.example/app/#stencil=%7B%7D'),
      'https://a.example/app/');
  });
});

test('anything that is not http(s) names no instance', async () => {
  await withHost({}, ({ webTarget }) => {
    for (const bad of ['file:///Users/me/index.html', 'javascript:alert(1)', 'data:text/html,x',
      'stencil://open?src=x', 'not a url', 'ftp://host/app']) {
      assert.equal(webTarget.normalizeWebUrl(bad), '', `${bad} must be refused`);
    }
    assert.match(webTarget.BAD_WEB_URL, /http\(s\)/);
  });
});

test('the URL comes from the SETTING — never from the document being edited', async () => {
  await withHost({ 'stencil.webUrl': 'http://127.0.0.1:9000/' }, ({ vscode, webTarget }) => {
    // The buffer names another host in prose and in a directive; neither is an instance.
    vscode.window.activeTextEditor = {
      document: makeDocument({ text: '# https://elsewhere.example/\n@source https://elsewhere.example/i.png:\n' }),
    };
    assert.equal(webTarget.webUrlFor(vscode), 'http://127.0.0.1:9000/');
  });
});

test('a setting that is not http(s) leaves the caller with nothing to open', async () => {
  await withHost({ 'stencil.webUrl': 'file:///tmp/app.html' }, ({ vscode, webTarget }) => {
    assert.equal(webTarget.webUrlFor(vscode), '');
  });
});
