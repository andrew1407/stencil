// Builds and runs the desktop capture binary (desktop/, STENCIL_DOCS_CAPTURE=ON) once per
// theme its shots ask for, then turns the theme-swap frames into the GIF. Offscreen: fonts
// render and the theme wipe plays there; only the dust flights are skipped.
//   node usecases/capture-runner/desktop.mjs [--only <name>]
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { loadCaptureConfig } from './lib/captureConfig.mjs';
import { outDir, repoPath, scratchDir, scratchPath } from './lib/paths.mjs';
import { makeShotRunner } from './lib/shot/runner.mjs';
import { pairNames } from './lib/theme/selector.mjs';
import { framesToGif, quantizePng } from './lib/gifTools.mjs';
import { sampleVideo } from './lib/sampleMedia.mjs';
import { startMediaServer } from './lib/servers.mjs';
import { startCollab } from './lib/collab.mjs';

const config = loadCaptureConfig('desktop');
const runner = makeShotRunner({ config, out: outDir('desktop') });
const FRAMES = scratchDir('desktop-frames');
const BUILD = process.env.STENCIL_DESKTOP_BUILD || repoPath('desktop', 'build');
const CLIP = config.get('clip');
// The chrome drags: frame strips the stills pass films step by step, one GIF each.
const GESTURES = config.get('gestures');
// The video shots open a real clip: made with ffmpeg into the scratch dir and served
// over http for the URL tab, so nothing binary is committed.
const clip = sampleVideo();
const media = await startMediaServer(clip.dir, config.get('mediaPort'));
const run = (cmd, args, env = {}) => execFileSync(cmd, args, { stdio: 'inherit', env: { ...process.env, ...env } });

// "Floor survey" on the collaboration server (lib/collab.mjs), seeded and then removed by this run;
// a copy an interrupted run left is known by its name and description. Its picture is made by ffmpeg.
async function seedServerProject(url, token) {
  const auth = { Authorization: `Bearer ${token}` };
  const seed = config.get('serverProject');
  const listed = await (await fetch(`${url}/projects`, { headers: auth })).json();
  for (const p of listed.projects ?? []) {
    if (p.name === seed.name && p.description === seed.description) {
      await fetch(`${url}/projects/${p.id}`, { method: 'DELETE', headers: auth });
    }
  }
  const png = scratchPath('desktop-server-project.png');
  run('ffmpeg', ['-y', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
    'color=c=0xdde3ea:s=960x640,drawgrid=w=80:h=80:t=1:c=0x9aa5b1@0.7,drawbox=x=160:y=120:w=640:h=400:t=8:c=0x2563eb',
    '-frames:v', '1', png]);
  const res = await fetch(`${url}/projects`, {
    method: 'POST',
    headers: { ...auth, 'Content-Type': 'application/json' },
    body: JSON.stringify({ ...seed, hasImage: true, imageW: 960, imageH: 640,
      originalContent: `data:image/png;base64,${fs.readFileSync(png).toString('base64')}` }),
  });
  if (!res.ok) { console.warn(`  server project not seeded (${res.status})`); return null; }
  return (await res.json()).id;
}
const collab = await startCollab(config);
const token = collab.mint ? await collab.mint() : null;
const serverProject = token ? await seedServerProject(collab.url, token) : null;

console.log('desktop build');
run('cmake', ['-S', repoPath('desktop'), '-B', BUILD, '-DSTENCIL_DOCS_CAPTURE=ON']);
run('cmake', ['--build', BUILD, '--config', 'Release', '--target', 'stencil_docs_capture', '-j', '4']);

// The offscreen platform's screen is 800x800 and the tall dialogs size off availableGeometry, so
// the plugin gets a 1440x1100-logical screen in DEVICE pixels, which QT_SCALE_FACTOR divides.
const SCREEN_PX = { w: 1440, h: 1100 };
const SCREEN_FILE = scratchPath('desktop-screen.json');
fs.writeFileSync(SCREEN_FILE, JSON.stringify({
  screens: [{ name: 'docs', x: 0, y: 0,
              width: SCREEN_PX.w * config.get('scaleFactor'),
              height: SCREEN_PX.h * config.get('scaleFactor'),
              logicalDpi: 96, logicalBaseDpi: 96, dpr: 1 }],
}));

// What config/shared.json says, handed to the Qt binary: it holds no URLs of its own. A real
// model answers the chat only when the run asks for one (config.realLlm()).
const env = {
  QT_QPA_PLATFORM: `offscreen:configfile=${SCREEN_FILE}`,
  STENCIL_DOCS_OUT: runner.out,
  STENCIL_DOCS_FRAMES: FRAMES,
  STENCIL_DOCS_ICON_URL: config.url('botIcon'),
  STENCIL_DOCS_ICON_FILE: repoPath(config.get('urls.localBotIcon')),
  STENCIL_DOCS_CLIP: clip.file,
  STENCIL_DOCS_CLIP_URL: media.url(clip.name),
  STENCIL_DOCS_PROMPT: config.prompt('stub'),
  STENCIL_DOCS_SCRIPT: config.get('canvas.script').join('\n'),
  STENCIL_DOCS_COPY_SERVER: collab.url,
  STENCIL_DOCS_REAL_PROMPT: config.prompt('real'),
  STENCIL_DOCS_PLAN: JSON.stringify(config.stubPlan('sepiaOutline')),
  ...(token ? { STENCIL_DOCS_SERVER_URL: collab.url, STENCIL_DOCS_SERVER_TOKEN: token } : {}),
  STENCIL_DOCS_SERVER_PROJECT: config.get('serverProject').name,
  ...(config.realLlm() ? { STENCIL_DOCS_REAL_LLM: '1' } : {}),
};
// MSVC's generators put the binary under a per-config directory and give it an extension;
// a single-config build has it straight in BUILD.
const exeName = process.platform === 'win32' ? 'stencil_docs_capture.exe' : 'stencil_docs_capture';
const binary = [path.join(BUILD, exeName), path.join(BUILD, 'Release', exeName)]
  .find((p) => fs.existsSync(p)) ?? path.join(BUILD, exeName);

// Every shot this run wants, grouped by the theme it is taken in.
const wantedNames = [
  ...config.get('pairs').flatMap((base) => pairNames(base)),
  ...config.get('stills'),
  ...GESTURES.names,
].filter((name) => runner.wanted(name));
for (const name of GESTURES.names) if (wantedNames.includes(name)) fs.rmSync(path.join(FRAMES, name), { recursive: true, force: true });
const themes = ['dark', 'light'].filter((theme) => wantedNames.some((name) => runner.themeOf(name) === theme));

// Qt Multimedia hands back no frame under the offscreen platform, so the clip shots take
// their own pass on the real one — the only shots here that want a window on the screen.
const VIDEO_SHOTS = config.get('videoShots');

const taken = [];
try {
  for (const theme of themes) {
    const all = wantedNames.filter((name) => runner.themeOf(name) === theme);
    for (const onScreen of [false, true]) {
      const shots = all.filter((name) => VIDEO_SHOTS.includes(name) === onScreen);
      if (!shots.length) continue;
      const where = onScreen ? 'on screen — a clip decodes nowhere else' : 'offscreen';
      console.log(`desktop stills, ${theme} (${config.get('scaleFactor')}x, motion off, ${where})`);
      const { QT_QPA_PLATFORM, ...rest } = env;
      run(binary, [], {
        ...(onScreen ? rest : env),
        STENCIL_DOCS_THEME: theme,
        STENCIL_DOCS_SHOTS: shots.join(','),
        // Offscreen reports dpr 1, so the scale factor makes those shots 2x; a real screen brings its own,
        // and forcing it there too halves the logical screen the dialogs size against.
        ...(onScreen ? {} : { QT_SCALE_FACTOR: String(config.get('scaleFactor')) }),
        STENCIL_NO_ANIM: '1',
      });
      taken.push(...shots);
    }
  }
  for (const name of taken) {
    const file = path.join(runner.out, `${name}.png`);
    if (fs.existsSync(file)) quantizePng(file);
    if (!GESTURES.names.includes(name) || !fs.existsSync(path.join(FRAMES, name))) continue;
    framesToGif(path.join(FRAMES, name), path.join(runner.out, `${name}.gif`),
      { ...config.gifLook, inFps: GESTURES.inFps, fps: GESTURES.fps });
    console.log(`  ${name}.gif`);
  }

  // The theme wipe at 1x with motion on: a 2x grab outlasts a frame.
  if (runner.wanted(CLIP.name)) {
    console.log('desktop theme clip (1x, motion on)');
    run(binary, [], {
      ...env,
      STENCIL_DOCS_MODE: 'clip',
      STENCIL_DOCS_THEME: CLIP.startTheme,
      STENCIL_DOCS_CLIP_MS: String(CLIP.ms),
      STENCIL_DOCS_CLIP_EVERY_MS: String(CLIP.everyMs),
    });
    framesToGif(path.join(FRAMES, CLIP.name), path.join(runner.out, `${CLIP.name}.gif`),
      { ...config.gifLook, inFps: CLIP.inFps, fps: CLIP.fps });
    console.log(`  ${CLIP.name}.gif`);
  }
} finally {
  if (serverProject) {
    await fetch(`${collab.url}/projects/${serverProject}`, { method: 'DELETE', headers: { Authorization: `Bearer ${token}` } })
      .catch(() => {});
  }
  await collab.stop();
}
media.stop();
await runner.finish();
