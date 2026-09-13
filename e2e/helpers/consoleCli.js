// Drives the CLI's console mode over stdin: piped input uses the plain reader — no raw-mode
// editor, no confirmation prompts — which is the README's documented scripting mode.
// Async spawn, NOT spawnSync: a stub LLM lives in the test process, and a sync child would
// block the event loop, so the stub could never answer the CLI.
import { spawn } from 'node:child_process';
import { CLI_BIN } from './cli.js';

/** Pipe `lines` into `stencil --console`. Returns { code, stdout, stderr, out }. */
export function runConsole(lines, { cwd, env = {}, timeout = 30_000 } = {}) {
  return new Promise((resolve) => {
    const child = spawn(CLI_BIN, ['--console'], { cwd, env: { ...process.env, ...env } });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', (d) => { stdout += d; });
    child.stderr.on('data', (d) => { stderr += d; });
    const timer = setTimeout(() => child.kill('SIGKILL'), timeout);
    child.on('close', (code) => {
      clearTimeout(timer);
      resolve({ code, stdout, stderr, out: stdout + stderr });
    });
    child.stdin.write(`${lines.join('\n')}\n`);
    child.stdin.end();
  });
}
