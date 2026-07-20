// Helpers for driving the Zig CLI as a black box — the same argv/outcome contract the
// MCP server and the Telegram bot depend on: a `wrote {path} ({w}x{h} px · {page})` line
// on STDERR and exit 0 on success; an `error: …` line on STDERR and a non-zero exit on
// failure.
import { spawnSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const HERE = path.dirname(fileURLToPath(import.meta.url));

// Point at a built binary via STENCIL_CLI, else the conventional zig-out location.
export const CLI_BIN = process.env.STENCIL_CLI || path.resolve(HERE, '../../cli/zig-out/bin/stencil');
export const cliAvailable = () => existsSync(CLI_BIN);

// Run the CLI. Returns { code, stdout, stderr, out } (out = stdout+stderr combined, since
// the human-readable outcome lines go to stderr).
export function runCli(args, { cwd } = {}) {
  const r = spawnSync(CLI_BIN, args, { cwd, encoding: 'utf8', timeout: 30_000 });
  const stdout = r.stdout || '';
  const stderr = r.stderr || '';
  return { code: r.status, stdout, stderr, out: stdout + stderr };
}

// Parse the canonical success line: `wrote <path> (<w>x<h> px · <page>)`.
export function parseWrote(output) {
  const m = /wrote\s+(.+?)\s+\((\d+)x(\d+)\s+px/.exec(output);
  if (!m) return null;
  return { path: m[1], w: Number(m[2]), h: Number(m[3]) };
}

// Read a PNG's real pixel dimensions straight from its IHDR (width @16, height @20, BE),
// so tests assert the actual written file — not just what the CLI claims on stderr.
export function pngSize(file) {
  const buf = readFileSync(file);
  if (buf.length < 24 || buf.readUInt32BE(0) !== 0x89504e47) throw new Error(`not a PNG: ${file}`);
  return { width: buf.readUInt32BE(16), height: buf.readUInt32BE(20) };
}
