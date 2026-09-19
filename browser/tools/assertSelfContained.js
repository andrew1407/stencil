// ── "Is it really one file?" check ──────────────────────────────
// Run over a built stencil.html — by tools/buildHtml.js after every build, and by CI.
// A single file that quietly needs a sibling, whose inline <script> got truncated by a
// stray `</script`, or that carries a local file it should not, still LOOKS fine. So assert.
//   node tools/assertSelfContained.js stencil.html
import { readFileSync, writeFileSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

// A reference the page can resolve on its own, with nothing next to it.
const SELF_CONTAINED = /^(?:data:|blob:|https?:|mailto:|#)/;

export const assertSelfContained = (file) => {
  const html = readFileSync(file, 'utf8');
  const problems = [];

  // Markup only — the inlined JS and CSS are full of href/src-looking strings of their own.
  const markup = html.replace(/<(script|style)\b[^>]*>[\s\S]*?<\/\1>/g, '');
  for (const [, ref] of markup.matchAll(/(?:src|href)="([^"]*)"/g)) {
    if (!SELF_CONTAINED.test(ref)) problems.push(`loads a sibling file: ${ref.slice(0, 80)}`);
  }

  const modules = [...html.matchAll(/<script type="module">([\s\S]*?)<\/script>/g)];
  if (modules.length !== 1) problems.push(`expected exactly 1 inline module script, found ${modules.length}`);
  if (!/<style>[\s\S]*?<\/style>/.test(html)) problems.push('no inline <style> — the CSS did not get folded in');

  // The only data: URIs the build makes are the two SVG icons; an inlined JSON asset means vite resolved a
  // `new URL(x, import.meta.url)` to a file on the builder's disk and baked it into the page.
  if (/data:application\/json/.test(html)) problems.push('an inlined JSON asset (a local file baked into the page)');

  // Parse the inline module the way the browser will. Catches a script cut short by an
  // unescaped `</script` inside a string or regex, which no amount of grepping would.
  if (modules.length === 1) {
    const scratch = join(tmpdir(), `stencil-selfcheck-${process.pid}.mjs`);
    try {
      writeFileSync(scratch, modules[0][1]);
      const { status, stderr } = spawnSync(process.execPath, ['--check', scratch], { encoding: 'utf8' });
      if (status !== 0) problems.push(`the inline module does not parse:\n${stderr.trim()}`);
    } finally {
      rmSync(scratch, { force: true });
    }
  }

  if (problems.length) {
    throw new Error(`${file} is not self-contained:\n  - ${problems.join('\n  - ')}`);
  }
};

// CLI use: `node tools/assertSelfContained.js <file>`
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const file = process.argv[2];
  if (!file) {
    console.error('usage: node tools/assertSelfContained.js <file.html>');
    process.exit(1);
  }
  try {
    assertSelfContained(file);
    console.log(`stencil: ${file} is self-contained`);
  } catch (err) {
    console.error(`stencil: ${err.message}`);
    process.exit(1);
  }
}
