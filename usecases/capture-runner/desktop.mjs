// Builds and runs the desktop capture binary (desktop/, STENCIL_DOCS_CAPTURE=ON) once per
// theme its shots ask for, then turns the theme-swap frames into the GIF. Offscreen: fonts
// render and the theme wipe plays there; only the dust flights are skipped.
//   node usecases/capture-runner/desktop.mjs [--only <name>]
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { loadCaptureConfig } from './lib/captureConfig.mjs';
import { outDir, repoPath, scratchDir } from './lib/paths.mjs';
import { makeShotRunner } from './lib/shotRunner.mjs';
import { pairNames } from './lib/themeSelector.mjs';
import { framesToGif, quantizePng } from './lib/gifTools.mjs';

const config = loadCaptureConfig('desktop');
const runner = makeShotRunner({ config, out: outDir('desktop') });
const FRAMES = scratchDir('desktop-frames');
const BUILD = process.env.STENCIL_DESKTOP_BUILD || repoPath('desktop', 'build');
const CLIP = config.get('clip');
const run = (cmd, args, env = {}) => execFileSync(cmd, args, { stdio: 'inherit', env: { ...process.env, ...env } });

console.log('desktop build');
run('cmake', ['-S', repoPath('desktop'), '-B', BUILD, '-DSTENCIL_DOCS_CAPTURE=ON']);
run('cmake', ['--build', BUILD, '--target', 'stencil_docs_capture', '-j']);

// What config/shared.json says, handed to the Qt binary: it holds no URLs of its own.
const token = config.serverToken();
const env = {
  QT_QPA_PLATFORM: 'offscreen',
  STENCIL_DOCS_OUT: runner.out,
  STENCIL_DOCS_FRAMES: FRAMES,
  STENCIL_DOCS_FAVICON_URL: config.url('favicon'),
  STENCIL_DOCS_ICON_URL: config.url('botIcon'),
  STENCIL_DOCS_PROMPT: config.prompt('stub'),
  STENCIL_DOCS_REAL_PROMPT: config.prompt('real'),
  STENCIL_DOCS_PLAN: JSON.stringify(config.stubPlan('sepiaOutline')),
  ...(token ? { STENCIL_DOCS_SERVER_URL: config.serverUrl, STENCIL_DOCS_SERVER_TOKEN: token } : {}),
};
const binary = path.join(BUILD, 'stencil_docs_capture');

// Every shot this run wants, grouped by the theme it is taken in.
const wantedNames = [
  ...config.get('pairs').flatMap((base) => pairNames(base)),
  ...config.get('stills'),
].filter((name) => runner.wanted(name));
const themes = ['dark', 'light'].filter((theme) => wantedNames.some((name) => runner.themeOf(name) === theme));

const taken = [];
for (const theme of themes) {
  const shots = wantedNames.filter((name) => runner.themeOf(name) === theme);
  console.log(`desktop stills, ${theme} (${config.get('scaleFactor')}x, motion off)`);
  run(binary, [], {
    ...env,
    STENCIL_DOCS_THEME: theme,
    STENCIL_DOCS_SHOTS: shots.join(','),
    QT_SCALE_FACTOR: String(config.get('scaleFactor')),
    STENCIL_NO_ANIM: '1',
  });
  taken.push(...shots);
}
for (const name of taken) {
  const file = path.join(runner.out, `${name}.png`);
  if (fs.existsSync(file)) quantizePng(file);
}

// The theme wipe at 1x with motion on: a 2x grab outlasts a frame.
if (runner.wanted(CLIP.name)) {
  console.log('desktop theme clip (1x, motion on)');
  run(binary, [], { ...env, STENCIL_DOCS_MODE: 'clip', STENCIL_DOCS_THEME: CLIP.startTheme });
  framesToGif(path.join(FRAMES, CLIP.name), path.join(runner.out, `${CLIP.name}.gif`),
    { ...config.gifLook, inFps: CLIP.inFps, fps: CLIP.fps });
  console.log(`  ${CLIP.name}.gif`);
}
runner.finish();
