// CLI .stc e2e: the one-shot --script / --script-check flags and the console's /script verb,
// driven with the SHARED fixture corpus (browser/js/config/script/fixtures/cases.txt), so the
// binary is proved on the very scripts the core's own suites use. Every case asserts the real
// written PNG, or the exact diagnostic line an editor parses — never the CLI's own claim.
import { test, expect } from '@playwright/test';
import path from 'node:path';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { runCli, parseWrote, pngSize, cliAvailable, makeBlankInput } from '../../helpers/cli.js';
import { runConsole } from '../../helpers/consoleCli.js';
import { stcCase, writeStcCase } from '../../helpers/stcCases.js';
import { APP_URL } from '../../helpers/config.js';

test.describe('cli .stc scripts', () => {
  test.skip(!cliAvailable(), 'build the CLI first: (cd cli && zig build) or set STENCIL_CLI');

  test('--script runs a sourceless script over -i and saves beside the input', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const input = makeBlankInput(dir, 20, 10);
    const script = writeStcCase('top-level-project', dir);   // @crop 10% · @filter bw · @save

    const r = runCli(['-i', input, '--script', script], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    // A bare @save lands next to its source with the -stencil suffix; 10% off each edge of
    // 20x10 leaves 16x8, and the file on disk — not the stderr line — says so.
    const wrote = parseWrote(r.out);
    expect(wrote).toMatchObject({ w: 16, h: 8 });
    expect(path.basename(wrote.path)).toBe('in-stencil.png');
    expect(pngSize(path.join(dir, 'in-stencil.png'))).toEqual({ width: 16, height: 8 });
  });

  test('--script-check on a clean script prints nothing and exits 0', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    writeStcCase('top-level-project', dir);
    const r = runCli(['--script-check', 'top-level-project.stc'], { cwd: dir });
    expect(r.code).toBe(0);
    expect(r.out.trim()).toBe('');
  });

  test('--script-check prints the corpus diagnostic in the editor grammar and exits 1', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const name = 'err-unknown-directive.stc';
    writeStcCase('err-unknown-directive', dir);
    const [diag] = stcCase('err-unknown-directive').diagnostics;

    const r = runCli(['--script-check', name], { cwd: dir });
    expect(r.code).toBe(1);
    // `file:line:col: severity: message [CODE]`, on STDOUT so an editor can read it.
    expect(r.stdout).toMatch(/^[^:]+:\d+:\d+: (error|warning): .+ \[[A-Z_]+\]$/m);
    expect(r.stdout.trim()).toBe(
      `${name}:${diag.line}:${diag.col}: ${diag.severity}: ${diag.message} [${diag.code}]`,
    );
  });

  test('--script-plan prints the op-plan envelope on stdout and writes nothing', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const input = makeBlankInput(dir, 20, 10);
    const script = writeStcCase('top-level-project', dir);   // @crop 10% · @filter bw · @save

    const r = runCli(['-i', input, '--script-plan', script], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    const envelope = JSON.parse(r.stdout);
    expect(envelope.version).toBe(1);
    expect(envelope.diagnostics).toEqual([]);
    expect(envelope.blocks).toHaveLength(1);
    const [block] = envelope.blocks;
    expect(block.sourceKind).toBe('project');
    expect(block.inputs).toEqual([input]);
    // The actions are the op-plan vocabulary the LLM path already validates.
    const ops = block.plans.flatMap((p) => p.actions).map((a) => a.op);
    expect(ops).toEqual(expect.arrayContaining(['crop', 'filter']));
    expect(block.saves.length).toBeGreaterThan(0);
    // Planning runs nothing: the @save is described, never performed.
    expect(existsSync(path.join(dir, 'in-stencil.png'))).toBeFalsy();
  });

  test('a directory source runs the same edits over every image in it', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    // Inline rather than from the corpus: a real directory has to hold real files, and the
    // corpus cases name paths that only have to parse.
    const shots = path.join(dir, 'shots');
    mkdirSync(shots, { recursive: true });
    // The save directory has to exist first: a script's @save writes into it, it does not
    // create it (unlike scrape mode's destination, which the CLI does create).
    mkdirSync(path.join(dir, 'out'), { recursive: true });
    for (const name of ['a.png', 'b.png']) {
      const r = runCli(['--blank', '20', '10', 'white', path.join(shots, name)], { cwd: dir });
      expect(r.code, r.out).toBe(0);
    }
    writeFileSync(path.join(dir, 'dir.stc'), '@source shots/:\n    @crop 10%\n    @save out/\n');

    const r = runCli(['--script', 'dir.stc'], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    for (const base of ['a', 'b']) {
      expect(pngSize(path.join(dir, 'out', `${base}-stencil.png`))).toEqual({ width: 16, height: 8 });
    }
  });

  test('a @source URL block is really fetched, edited and saved', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    // The harness's own static server, not the internet: the block goes through the CLI's
    // fetch guard (cli/src/net.zig) and decodes the bytes it gets back.
    const url = `${APP_URL}__e2e__/pixel.png`;   // the 3x2 fixture
    writeFileSync(path.join(dir, 'url.stc'), `@source ${url}:\n    @filter bw\n    @save fetched.png\n`);

    const r = runCli(['--script', 'url.stc'], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    expect(pngSize(path.join(dir, 'fetched.png'))).toEqual({ width: 3, height: 2 });
  });

  test('the console runs a ;-separated one-liner against the loaded image', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    makeBlankInput(dir, 20, 10);
    const { script } = stcCase('semicolon-oneliner');   // @crop 25%;@filter bw;@save out.png

    const r = await runConsole([
      '/upload in.png',
      `/script ${script}`,
      '/save console.png',
      '/exit',
    ], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    // 25% off each edge of 20x10 leaves 10x5, and only /save writes: the console owns a
    // session, not files, so the script's own @save is reported and skipped.
    expect(pngSize(path.join(dir, 'console.png'))).toEqual({ width: 10, height: 5 });
    expect(existsSync(path.join(dir, 'out.png'))).toBeFalsy();
  });
});
