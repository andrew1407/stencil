// ── moveCheck: did a JS refactor MOVE code, or change it? ────
// Hashes every top-level function / class-method / arrow-const body at <gitRef> and in the
// working tree — comments stripped, whitespace collapsed — then diffs the two multisets.
//   node tools/moveCheck.mjs <gitRef> <path...>   (paths: files or dirs; exits 1 on LOST)
//   node tools/moveCheck.mjs --self-test
import { pathToFileURL } from 'node:url';
import path from 'node:path';
import { blobsAt, git, listFiles, readAll, repoRoot } from './repo/io.mjs';
import { selfTest } from './moveCheck/selfTest.mjs';
import { diffUnits, unitsOf } from './moveCheck/units.mjs';

export { langFor, langOf, mask, normalize, scan, stripComments } from './moveCheck/scan.mjs';
export { diffUnits, unitsOf } from './moveCheck/units.mjs';

// ── git + fs plumbing ───────────────────────────────────────────
const isJs = (p) => ['.js', '.mjs'].includes(path.extname(p).toLowerCase());

// Repo-relative JS paths on both sides: the working tree now (tracked + new, never
// gitignored build output), the tree at <ref> then — so a file the move deleted still counts.
const listNow = async (root, rel) => (await listFiles(root, [], rel)).filter(isJs).sort();
const listThen = async (root, ref, rel) => (await git(root, 'ls-tree', '-r', '--name-only', ref, '--', ...rel))
  .split('\n').filter((f) => f && isJs(f)).sort();

const run = async (ref, args) => {
  const root = await repoRoot();
  const rel = args.map((a) => path.relative(root, path.resolve(a)) || '.');
  const [now, then] = await Promise.all([listNow(root, rel), listThen(root, ref, rel)]);
  const [nowTexts, thenTexts] = await Promise.all([readAll(root, now), blobsAt(root, ref, then)]);

  const headUnits = now.flatMap((f, i) => unitsOf(f, nowTexts[i]));
  const refUnits = then.flatMap((f) => unitsOf(f, thenTexts.get(f) ?? ''));
  const { lost, gained, unchanged } = diffUnits(refUnits, headUnits);

  const at = (u) => `${u.hash}  ${u.file}:${u.line} ${u.name}`;
  console.log(`moveCheck ${ref} -> working tree  (${then.length} files then, ${now.length} now)`);
  for (const u of lost) console.log(`  LOST  ${at(u)}`);
  for (const u of gained) console.log(`  NEW   ${at(u)}`);
  console.log(`  unchanged ${unchanged}   LOST ${lost.length}   NEW ${gained.length}`);
  return lost.length ? 1 : 0;
};

if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const args = process.argv.slice(2);
  if (args[0] === '--self-test') await selfTest();
  else if (args.length < 2) { console.error('usage: node tools/moveCheck.mjs <gitRef> <path...>   |   --self-test'); process.exit(2); }
  else process.exit(await run(args[0], args.slice(1)));
}
