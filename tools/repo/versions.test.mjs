// versions.mjs: the live tree repeats each manifest's version in its lockfile, badges and
// changelogs; a drifted lockfile or badge is rewritten, a changelog that lags is only reported.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { cpSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { APPS, CHANGELOG, files, run, versionOf } from './versions.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

const scratchTree = () => {
  const dir = mkdtempSync(path.join(tmpdir(), 'versions-'));
  for (const rel of files()) {
    mkdirSync(path.join(dir, path.dirname(rel)), { recursive: true });
    cpSync(path.join(ROOT, rel), path.join(dir, rel));
  }
  return dir;
};

const edit = (dir, rel, from, to) => {
  const file = path.join(dir, rel);
  const text = readFileSync(file, 'utf8');
  assert.ok(text.includes(from), `${rel} holds ${from}`);
  writeFileSync(file, text.replace(from, to));
};

const inScratch = (fn) => {
  const dir = scratchTree();
  try { fn(dir); } finally { rmSync(dir, { recursive: true, force: true }); }
};

test('the live tree: every lockfile, badge and changelog repeats its manifest', () => {
  assert.deepEqual(run({ root: ROOT }).problems, []);
});

test('every app has a badge label, a changelog of its own and a row in the root one', () => {
  const root = readFileSync(path.join(ROOT, CHANGELOG), 'utf8');
  for (const [app, { label, title }] of Object.entries(APPS)) {
    assert.match(label, /^[a-z_]+$/, app);
    assert.ok(root.includes(`[${title}](${app}/${CHANGELOG})`), `${CHANGELOG} links ${title}`);
    assert.match(readFileSync(path.join(ROOT, app, CHANGELOG), 'utf8'), /^# Changelog — /, app);
  }
});

test('a drifted lockfile or badge is reported, then rewritten from the manifest', () => inScratch((dir) => {
  edit(dir, 'mcp/Cargo.lock', 'name = "stencil-mcp"\nversion = "1.0.0"', 'name = "stencil-mcp"\nversion = "0.9.0"');
  edit(dir, 'browser/package-lock.json', '"version": "1.0.0"', '"version": "1.0.1"');
  edit(dir, 'README.md', 'badge/cli-1.0.0-', 'badge/cli-1.1.0-');
  assert.deepEqual(run({ root: dir }).problems.sort(), [
    'README.md: cli badge is 1.1.0, cli/build.zig.zon says 1.0.0',
    'browser/package-lock.json: browser version is 1.0.1, browser/package.json says 1.0.0',
    'mcp/Cargo.lock: mcp version is 0.9.0, mcp/Cargo.toml says 1.0.0',
  ]);
  const fixed = run({ root: dir, write: true });
  assert.deepEqual(fixed.problems, []);
  assert.deepEqual(fixed.written.sort(), ['README.md', 'browser/package-lock.json', 'mcp/Cargo.lock']);
  assert.deepEqual(run({ root: dir }).problems, []);
}));

test('an app moves alone: its badges follow, its changelog entries are asked for', () => inScratch((dir) => {
  edit(dir, 'cli/build.zig.zon', '.version = "1.0.0"', '.version = "1.2.3"');
  const lagging = [
    'CHANGELOG.md: cli row is 1.0.0, cli/build.zig.zon says 1.2.3',
    'cli/CHANGELOG.md: newest release heading is 1.0.0, cli/build.zig.zon says 1.2.3',
  ];
  const fixed = run({ root: dir, write: true });
  assert.deepEqual(fixed.written.sort(), ['README.md', 'cli/README.md']);
  assert.deepEqual(fixed.problems.sort(), lagging);
  assert.match(readFileSync(path.join(dir, 'README.md'), 'utf8'), /badge\/cli-1\.2\.3-.*\n.*badge\/pystencil-1\.0\.0-/);
  edit(dir, 'cli/CHANGELOG.md', '## [1.0.0]', '## [1.2.3]\n\n## [1.0.0]');
  edit(dir, CHANGELOG, '| [CLI](cli/CHANGELOG.md) | `cli/` | 1.0.0 |', '| [CLI](cli/CHANGELOG.md) | `cli/` | 1.2.3 |');
  assert.deepEqual(run({ root: dir }).problems, []);
}));

test("Stencil's own version is the root changelog's newest heading", () => inScratch((dir) => {
  edit(dir, CHANGELOG, '## [1.0.0]', '## [1.1.0]\n\n## [1.0.0]');
  assert.deepEqual(run({ root: dir }).problems, ['README.md: stencil badge is 1.0.0, CHANGELOG.md says 1.1.0']);
  assert.deepEqual(run({ root: dir, write: true }), { problems: [], written: ['README.md'] });
}));

test('a missing badge or manifest version is reported, never guessed', () => inScratch((dir) => {
  edit(dir, 'bot/README.md', 'badge/bot-', 'badge/robot-');
  edit(dir, 'server/cmd/stencil-server/main.go', 'const version =', 'const release =');
  assert.deepEqual(run({ root: dir, write: true }).problems.sort(), [
    'bot/README.md: no bot badge',
    'server/cmd/stencil-server/main.go: no server version',
  ]);
}));

test('--get reads one version where it is declared, and names no version it cannot find', () => inScratch((dir) => {
  edit(dir, 'server/cmd/stencil-server/main.go', 'const version = "1.0.0"', 'const version = "2.4.6"');
  edit(dir, CHANGELOG, '## [1.0.0]', '## [1.1.0]\n\n## [1.0.0]');
  assert.equal(versionOf('server', dir), '2.4.6');
  assert.equal(versionOf('stencil', dir), '1.1.0');
  assert.equal(versionOf('cli', dir), '1.0.0');
  assert.equal(versionOf('desktop-app', dir), null);
  edit(dir, 'bot/src/Stencil.TelegramBot.Bot/Stencil.TelegramBot.Bot.csproj', '<Version>', '<FileVersion>');
  assert.equal(versionOf('bot', dir), null);
}));
