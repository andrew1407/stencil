// The two lib/ units the features share: the memoized import() and the per-version parse cache.
import test from 'node:test';
import assert from 'node:assert/strict';

import { installVscodeStub, makeDocument, makeVscode } from './helpers/vscodeStub.js';

const withLib = async (body) => {
  const { vscode } = makeVscode();
  const host = installVscodeStub(vscode);
  try {
    return await body({
      host,
      parserHost: host.require('lib/parserHost.js'),
      programCache: host.require('lib/programCache.js'),
    });
  } finally {
    host.restore();
  }
};

test('the parser module graph is imported once and shared', async () => {
  await withLib(async ({ parserHost }) => {
    const first = parserHost.loadParser();
    assert.equal(parserHost.loadParser(), first, 'one promise, memoized');
    assert.equal(typeof (await first).parseScript, 'function');
  });
});

test('a rejected import is not memoized — the next parse tries again', async () => {
  await withLib(async ({ parserHost }) => {
    let attempts = 0;
    const failing = () => { attempts += 1; return Promise.reject(new Error('EBUSY')); };
    await assert.rejects(parserHost.loadParser(failing), /EBUSY/);
    await assert.rejects(parserHost.loadParser(failing), /EBUSY/);
    assert.equal(attempts, 2, 'the failure was not memoized');
    assert.equal(typeof (await parserHost.loadParser()).parseScript, 'function',
      'the real module still loads after a failure');
  });
});

test('one parse serves both features at one document version', async () => {
  await withLib(async ({ programCache }) => {
    let reads = 0;
    const document = {
      version: 3,
      uri: { toString: () => 'file:///a.stc' },
      getText: () => { reads += 1; return '@source a.png:\n    @crop 10%\n'; },
    };
    const first = await programCache.programFor(document);
    const second = await programCache.programFor(document);
    assert.equal(second, first, 'the same program object');
    assert.equal(reads, 1, 'the buffer was lexed once');
  });
});

test('a new version parses again, and a closed document is forgotten', async () => {
  await withLib(async ({ programCache }) => {
    const at = (version, text) => ({
      version, uri: { toString: () => 'file:///a.stc' }, getText: () => text,
    });
    const one = await programCache.programFor(at(1, '@source a.png:\n'));
    const two = await programCache.programFor(at(2, '@source b.png:\n'));
    assert.notEqual(two, one);
    assert.equal(await programCache.programFor(at(1, '@source a.png:\n')), one, 'still cached');
    programCache.forget({ toString: () => 'file:///a.stc' });
    assert.notEqual(await programCache.programFor(at(1, '@source a.png:\n')), one, 'dropped');
  });
});

test('a document with no version is parsed fresh every time', async () => {
  await withLib(async ({ programCache }) => {
    const document = makeDocument({ text: '@source a.png:\n' });
    const first = await programCache.programFor(document);
    assert.notEqual(await programCache.programFor(document), first, 'nothing would invalidate it');
  });
});

test('the cache keeps only the most recent versions', async () => {
  await withLib(async ({ programCache }) => {
    const at = (version) => ({
      version, uri: { toString: () => 'file:///a.stc' }, getText: () => `# ${version}\n`,
    });
    const oldest = await programCache.programFor(at(0));
    for (let version = 1; version <= programCache.LIMIT; version += 1) {
      await programCache.programFor(at(version));
    }
    assert.notEqual(await programCache.programFor(at(0)), oldest, 'the oldest was evicted');
  });
});
