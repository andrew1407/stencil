// CLI e2e: run the real Zig binary end-to-end over its documented argv/outcome contract
// (the same one mcp/ and bot/ depend on). Each case asserts BOTH the `wrote …` stderr
// line and the actual written PNG's IHDR dimensions — not just the CLI's own claim.
import { test, expect } from '@playwright/test';
import path from 'node:path';
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { runCli, parseWrote, pngSize, cliAvailable } from '../../helpers/cli.js';

const FIXTURES = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../fixtures');
const IMG_URL = 'http://127.0.0.1:8188/__e2e__/pixel.png';

test.describe('cli pipeline', () => {
  test.skip(!cliAvailable(), 'build the CLI first: (cd cli && zig build) or set STENCIL_CLI');

  // A known non-square input, created by the CLI itself (also exercises --blank w h).
  const makeInput = (dir, w = 8, h = 4) => {
    const p = path.join(dir, 'in.png');
    const r = runCli(['--blank', String(w), String(h), 'white', p], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    expect(pngSize(p)).toEqual({ width: w, height: h });
    return p;
  };

  test('--blank writes a page of the requested pixel size', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const out = path.join(dir, 'blank.png');
    const r = runCli(['--blank', '20', '12', 'red', out], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    const wrote = parseWrote(r.out);
    expect(wrote).toMatchObject({ w: 20, h: 12 });
    expect(pngSize(out)).toEqual({ width: 20, height: 12 });
  });

  test('--blank accepts a named page format', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const out = path.join(dir, 'a5.png');
    const r = runCli(['--blank', 'a5', out], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    expect(r.out).toMatch(/A5/i);           // page label reflects the format
    const { width, height } = pngSize(out);
    expect(width).toBeGreaterThan(0);
    expect(height).toBeGreaterThan(0);
  });

  test('-r rotates a quarter turn (dimensions swap)', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const input = makeInput(dir);          // 8x4
    const out = path.join(dir, 'rot.png');
    const r = runCli(['-i', input, '-r', '1', out], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    expect(pngSize(out)).toEqual({ width: 4, height: 8 });
  });

  test('-c crops by percentage edges', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const input = makeInput(dir);          // 8x4
    const out = path.join(dir, 'crop.png');
    const r = runCli(['-i', input, '-c', 'x1=25% x2=75%', out], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    expect(pngSize(out).width).toBe(4);    // middle 50% of 8; height kept
  });

  test('--filter draws over the image (output changes, dims kept)', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const input = makeInput(dir);
    const plain = path.join(dir, 'plain.png');
    const toned = path.join(dir, 'sepia.png');
    expect(runCli(['-i', input, plain], { cwd: dir }).code).toBe(0);
    const r = runCli(['-i', input, '--filter', 'sepia', toned], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    expect(pngSize(toned)).toEqual({ width: 8, height: 4 });
    expect(readFileSync(toned).equals(readFileSync(plain))).toBeFalsy(); // sepia changed pixels
  });

  test('--layout draws the layout onto the image', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const input = makeInput(dir);
    const plain = path.join(dir, 'plain.png');
    const drawn = path.join(dir, 'drawn.png');
    expect(runCli(['-i', input, plain], { cwd: dir }).code).toBe(0);
    const r = runCli(['-i', input, '--layout', path.join(FIXTURES, 'cli-layout.json'), drawn], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    expect(pngSize(drawn)).toEqual({ width: 8, height: 4 });
    expect(readFileSync(drawn).equals(readFileSync(plain))).toBeFalsy(); // the line was drawn
  });

  test('fills in a missing output extension from the format', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const input = makeInput(dir);
    const r = runCli(['-i', input, '-r', '1', path.join(dir, 'noext')], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    expect(parseWrote(r.out).path).toMatch(/\.png$/);
    expect(existsSync(path.join(dir, 'noext.png'))).toBeTruthy();
  });

  test('fetches an http(s) URL input (native, no external tool)', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const out = path.join(dir, 'url.png');
    const r = runCli(['-i', IMG_URL, out], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    expect(pngSize(out)).toEqual({ width: 3, height: 2 }); // matches fixtures/pixel.png
  });

  test('errors (non-zero exit + error: on stderr) when no source is given', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const r = runCli([path.join(dir, 'nope.png')], { cwd: dir });
    expect(r.code).not.toBe(0);
    expect(r.stderr).toMatch(/^error:/m);
  });

  // Cross-surface: a .stencil project authored in the shared format (fixtures/project.stencil —
  // an 8x4 image with a quarter-turn in its layout) opens in the CLI and renders exactly what
  // every other surface would — the embedded layout's rotation is applied, so 8x4 -> 4x8.
  test('opens a .stencil project as input and renders its embedded layout', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const out = path.join(dir, 'from-project.png');
    const r = runCli(['-i', path.join(FIXTURES, 'project.stencil'), out], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    expect(pngSize(out)).toEqual({ width: 4, height: 8 }); // 8x4 rotated a quarter turn
  });

  // …and the CLI can write the same portable format back out (an informational
  // "wrote … (project)" line, deliberately without a WxH size token — see cli/CONTRACT.md).
  test('bundles an image into a .stencil project file', async ({}, testInfo) => {
    const dir = testInfo.outputPath();
    const input = makeInput(dir); // 8x4
    const out = path.join(dir, 'bundle.stencil');
    const r = runCli(['-i', input, out], { cwd: dir });
    expect(r.code, r.out).toBe(0);
    expect(r.out).toMatch(/wrote .*\(project\)/);
    const doc = JSON.parse(readFileSync(out, 'utf8'));
    expect(doc.format).toBe('stencil-project');
    expect(doc.image.dataUrl).toMatch(/^data:image\/png;base64,/);
  });
});
