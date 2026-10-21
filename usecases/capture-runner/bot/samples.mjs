// The files the upload shots send, written fresh under the scratch dir on every run so nothing
// binary is committed: a .stc script, a .json layout, a .stencil project the CLI writes from the
// picture and that layout, and the short clip lib/sampleMedia.mjs makes.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { repoPath, scratchDir } from '../lib/paths.mjs';
import { sampleVideo } from '../lib/sampleMedia.mjs';

const SCRIPT = [
  '# Frame the picture and warm it up',
  '@crop 8%',
  '@filter sepia',
  '@use line #e11d48, 6px, dashed',
  '@rect (12%, 12%) (88%, 88%)',
].join('\n');

// Pixels of the 512x512 bot icon: a box round the paper plane and a line leading to it.
const LAYOUT = Object.freeze({
  imageWidth: 512,
  imageHeight: 512,
  lines: [
    { points: [{ x: 336, y: 336 }, { x: 500, y: 336 }, { x: 500, y: 500 }, { x: 336, y: 500 }, { x: 336, y: 336 }],
      color: '#e11d48', thickness: 6, style: 'dashed' },
    { points: [{ x: 70, y: 70 }, { x: 320, y: 320 }], color: '#2563eb', thickness: 5, style: 'solid' },
  ],
});

const CLI = repoPath('cli', 'zig-out', 'bin', process.platform === 'win32' ? 'stencil.exe' : 'stencil');

export function makeSamples(picture) {
  const dir = scratchDir('bot-samples');
  const script = path.join(dir, 'frame-and-warm.stc');
  fs.writeFileSync(script, `${SCRIPT}\n`);
  const layout = path.join(dir, 'plane-marks.json');
  fs.writeFileSync(layout, `${JSON.stringify(LAYOUT, null, 2)}\n`);
  const project = path.join(dir, 'Kitchen plan.stencil');
  execFileSync(CLI, ['-i', picture, '--layout', layout, '--filter', 'bw', project], { stdio: 'ignore' });
  return { script, layout, project, video: sampleVideo().file };
}
