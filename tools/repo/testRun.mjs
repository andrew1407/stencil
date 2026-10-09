// `node --test` sized to this machine: half its cores, at least 1 and at most 6, so a suite
// leaves the machine usable. STENCIL_TEST_CONCURRENCY overrides; other arguments pass through.
//   node ../tools/repo/testRun.mjs [node --test args...]   (run from a package folder)
import { spawn } from 'node:child_process';
import { availableParallelism } from 'node:os';
import { pathToFileURL } from 'node:url';

export const MAX_WORKERS = 6;

export const testWorkers = (cores = availableParallelism(), override = process.env.STENCIL_TEST_CONCURRENCY) => {
  const asked = Number.parseInt(override ?? '', 10);
  if (Number.isInteger(asked) && asked > 0) return asked;
  return Math.max(1, Math.min(MAX_WORKERS, Math.floor(cores / 2)));
};

if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const child = spawn(process.execPath, ['--test', `--test-concurrency=${testWorkers()}`, ...process.argv.slice(2)],
    { stdio: 'inherit' });
  // Ctrl-C reaches the child through the terminal; the runner only waits for it and mirrors its end.
  for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, () => child.kill(sig));
  child.on('error', (err) => { console.error(err.message); process.exit(1); });
  child.on('exit', (code, signal) => process.exit(code ?? (signal ? 1 : 0)));
}
