// CLI e2e over the stderr grammar adapters parse (cli/CONTRACT.md §1, §2.3, §8): a layout that
// is no layout object is one `error:` line and exit 1, an invalid --filter or whole number is an
// argv refusal (exit 2) before anything runs, and --merge-lines answers one JSON document on stdout.
import { test, expect } from '@playwright/test';
import path from 'node:path';
import { existsSync, writeFileSync } from 'node:fs';
import { runCli, cliAvailable } from '../../helpers/cli/run.js';

const errorLines = (stderr) => stderr.split(/\r?\n/).map((l) => l.trim()).filter((l) => l.startsWith('error:'));

test.describe('cli contract errors', () => {
  test.skip(!cliAvailable(), 'build the CLI first: (cd cli && zig build) or set STENCIL_CLI');

  test('a malformed or non-object layout is named in one error line, exit 1, nothing written', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const cases = [
      ['bad.json', '{"lines":[', 'UnexpectedEndOfInput'],
      ['arr.json', '[]', 'not a JSON object'],
      ['line.json', '{"lines":[{"points":[],"thickness":-1}]}', 'lines[0].thickness must be a number >= 0'],
    ];
    for (const [name, json, why] of cases) {
      writeFileSync(path.join(dir, name), json);
      const r = runCli(['--blank', '8', '8', '-l', name, 'o.png'], { cwd: dir });
      expect(r.code, r.out).toBe(1);
      expect(errorLines(r.stderr)).toEqual([`error: could not read layout '${name}' (${why})`]);
      expect(existsSync(path.join(dir, 'o.png'))).toBe(false);
    }
  });

  test('an invalid --filter is refused before anything runs, exit 2; a colour still tints', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    for (const bad of ['#ggg', 'notacolor', '']) {
      const r = runCli(['--blank', '8', '8', '--filter', bad, 'o.png'], { cwd: dir });
      expect(r.code, r.out).toBe(2);
      expect(errorLines(r.stderr)).toEqual([
        `error: --filter expects bw, invert, contour, sepia, a colour name or #hex, got '${bad}'`,
      ]);
      expect(existsSync(path.join(dir, 'o.png'))).toBe(false);
    }
    const ok = runCli(['--blank', '8', '8', '--filter', '#7c3aed', 'o.png'], { cwd: dir });
    expect(ok.code, ok.out).toBe(0);
  });

  test('a bad whole number names its flag and what it got, exit 2, nothing written', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    for (const [flag, bad] of [['--rotate', '1.5'], ['--frame', 'x'], ['--thumbnail', '-4']]) {
      const r = runCli(['--blank', '8', '8', flag, bad, 'o.png'], { cwd: dir });
      expect(r.code, r.out).toBe(2);
      expect(errorLines(r.stderr)).toEqual([`error: ${flag} expects a whole number, got '${bad}'`]);
      expect(existsSync(path.join(dir, 'o.png'))).toBe(false);
    }
  });

  test('--merge-lines - joins peer and local lines into one envelope on stdout', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const line = (x) => ({ points: [{ x, y: x }] });
    const input = JSON.stringify({ peer: [line(1)], local: [line(1), line(2)], seen: [] });
    const r = runCli(['--merge-lines', '-'], { cwd: dir, input });
    expect(r.code, r.out).toBe(0);
    expect(r.stdout.endsWith('}\n')).toBe(true);
    expect(JSON.parse(r.stdout)).toEqual({ version: 1, lines: [line(1), line(2)], peerAdded: false });

    const refused = runCli(['--merge-lines', '-'], { cwd: dir, input: '[]' });
    expect(refused.code, refused.out).toBe(2);
    expect(refused.stdout).toBe('');
    expect(errorLines(refused.stderr)[0]).toMatch(/^error: the merge input is not a JSON object/);
  });
});
