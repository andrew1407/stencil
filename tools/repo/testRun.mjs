// `node --test` sized to this machine: half its cores, at least 1 and at most 6, so a suite
// leaves the machine usable. STENCIL_TEST_CONCURRENCY overrides; other arguments pass through.
// A bare run in a package whose package.json sets `stencil.testFloor` also holds the total to it.
//   node ../tools/repo/testRun.mjs [node --test args...]   (run from a package folder)
import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { availableParallelism, tmpdir } from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

export const MAX_WORKERS = 6;

export const testWorkers = (cores = availableParallelism(), override = process.env.STENCIL_TEST_CONCURRENCY) => {
  const asked = Number.parseInt(override ?? '', 10);
  if (Number.isInteger(asked) && asked > 0) return asked;
  return Math.max(1, Math.min(MAX_WORKERS, Math.floor(cores / 2)));
};

// The floor a bare run in `dir` answers to, or null: a filtered or file-scoped run counts a fraction.
export const floorFor = (dir, args) => {
  const manifest = path.join(dir, 'package.json');
  if (args.length || !existsSync(manifest)) return null;
  const floor = JSON.parse(readFileSync(manifest, 'utf8')).stencil?.testFloor;
  return Number.isInteger(floor) && floor > 0 ? floor : null;
};

// The `# tests N` total of a TAP report, or null when the run printed none.
export const tapTotal = (tap) => {
  const m = /^# tests (\d+)$/m.exec(tap);
  return m ? Number(m[1]) : null;
};

export const floorVerdict = (name, total, floor) => {
  if (total === null) return `${name}: the runner printed no test total`;
  return total < floor ? `${name} suite collapsed to ${total} tests, floor is ${floor}` : null;
};

if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const args = process.argv.slice(2);
  const floor = floorFor(process.cwd(), args);
  const scratch = floor === null ? null : mkdtempSync(path.join(tmpdir(), 'stencil-tap-'));
  const tapFile = scratch && path.join(scratch, 'report.tap');
  const reporters = tapFile
    ? ['--test-reporter=spec', '--test-reporter-destination=stdout',
      '--test-reporter=tap', `--test-reporter-destination=${tapFile}`]
    : [];
  const child = spawn(process.execPath,
    ['--test', `--test-concurrency=${testWorkers()}`, ...reporters, ...args], { stdio: 'inherit' });
  // Ctrl-C reaches the child through the terminal; the runner only waits for it and mirrors its end.
  for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, () => child.kill(sig));
  child.on('error', (err) => { console.error(err.message); process.exit(1); });
  child.on('exit', (code, signal) => {
    let status = code ?? (signal ? 1 : 0);
    if (tapFile) {
      const tap = existsSync(tapFile) ? readFileSync(tapFile, 'utf8') : '';
      const problem = floorVerdict(path.basename(process.cwd()), tapTotal(tap), floor);
      if (problem && !signal) { console.error(`test-count floor: ${problem}`); status = 1; }
      rmSync(scratch, { recursive: true, force: true });
    }
    process.exit(status);
  });
}
