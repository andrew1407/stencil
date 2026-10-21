// Photographs the CLI's and pystencil's terminal scenes, headless: cli_sessions.py records the real
// binaries in a pseudo-terminal and writes HTML stills and timed frames; this starts the canned
// assistant and reaches the collaboration server those scenes use, then shoots the pages.
//   node usecases/capture-runner/cliTerminal.mjs [cli|pystencil] [--only <name>[,<name>…]]
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { chromium } from './lib/playwright.mjs';
import { loadCaptureConfig } from './lib/captureConfig.mjs';
import { CAPTURE, outDir, scratchPath } from './lib/paths.mjs';
import { makeShotRunner } from './lib/shot/runner.mjs';
import { castToGif, quantizePng } from './lib/gifTools.mjs';
import { startLlmStub } from './lib/llmStub.mjs';
import { startCollab } from './lib/collab.mjs';

// The arrow lib/shot/pointer.mjs draws, for a frame just after a click on a terminal cell.
const ARROW = '<svg width="22" height="30"><path d="M2 2 L2 24 L8 18.5 L12 27.5 L15.5 26 L11.6 17.2 L19.5 17.2 Z" '
  + 'fill="#fff" stroke="#111" stroke-width="1.6" stroke-linejoin="round"/></svg>';
const pointAt = (page, [row, col]) => page.evaluate(([r, c, svg]) => {
  const term = document.querySelector('.term');
  const pre = term.querySelector('pre');
  const probe = pre.appendChild(document.createElement('span'));
  probe.textContent = 'M';
  const cell = probe.getBoundingClientRect();
  probe.remove();
  const at = pre.getBoundingClientRect(), origin = term.getBoundingClientRect();
  const mark = term.appendChild(document.createElement('div'));
  mark.innerHTML = svg;
  mark.style.cssText = `position:absolute;left:${at.left - origin.left + (c - 0.5) * cell.width}px;`
    + `top:${at.top - origin.top + (r - 0.5) * cell.height}px`;
  term.style.position = 'relative';
}, [row, col, ARROW]);

// A GIF frame is shot square and opaque: a transparent corner keeps the encoder from storing only
// what changed, and every frame then weighs a whole picture.
const photograph = async (page, html, png, { opaque = false, pointer = null } = {}) => {
  await page.goto(pathToFileURL(html).href);
  if (opaque) await page.addStyleTag({ content: '.term{border-radius:0;box-shadow:none}' });
  if (pointer) await pointAt(page, pointer);
  await page.locator('.term').screenshot({ path: png, omitBackground: !opaque });
};

// cli_sessions.py records the scenes; the canned assistant answers from this very process, so the
// recorder runs asynchronously. What a server scene created is deleted again afterwards.
async function record(app, config, scenes) {
  const stub = await startLlmStub();
  for (const scene of scenes) for (const plan of scene.plans ?? []) stub.queue(config.stubPlan(plan));
  const collab = scenes.some((scene) => scene.server) ? await startCollab(config) : null;
  const token = collab?.mint ? await collab.mint() : '';
  const recorder = spawn('python3', [path.join(CAPTURE, 'cli_sessions.py'), app, '--stub', stub.url,
    '--server', collab?.url ?? '', '--only', scenes.map((scene) => scene.name).join(',')],
  { stdio: 'inherit', env: { ...process.env, STENCIL_DOCS_SESSION_TOKEN: token } });
  const code = await new Promise((resolve) => recorder.on('exit', resolve));
  for (const name of token ? config.get('sessions.serverNames') ?? [] : []) {
    const auth = { Authorization: `Bearer ${token}` };
    const { projects } = await (await fetch(`${collab.url}/projects`, { headers: auth })).json();
    for (const p of projects.filter((project) => project.name === name)) {
      await fetch(`${collab.url}/projects/${p.id}`, { method: 'DELETE', headers: auth });
    }
  }
  await stub.close();
  await collab?.stop();
  if (code !== 0) throw new Error(`cli_sessions.py ${app} exited ${code}`);
}

// A still is one page; a clip a folder of frames with the moment each was painted; a kept file
// is what a command wrote, shown as it is.
async function photographAll(config, runner, source) {
  const browser = await chromium.launch();
  const { width, height, scaleFactor } = config.get('page');
  const page = await browser.newPage({ viewport: { width, height }, deviceScaleFactor: scaleFactor });
  for (const name of fs.readdirSync(source).filter((f) => runner.wanted(f)).sort()) {
    const at = path.join(source, name);
    if (name.endsWith('.html')) {
      const png = path.join(runner.out, name.replace(/\.html$/, '.png'));
      await photograph(page, at, png);
      quantizePng(png);
      console.log(`  ${path.basename(png)}`);
    } else if (name.endsWith('.png')) {
      fs.copyFileSync(at, path.join(runner.out, name));
      console.log(`  ${name}`);
    } else if (fs.existsSync(path.join(at, 'frames.json'))) {
      const cast = JSON.parse(fs.readFileSync(path.join(at, 'frames.json'), 'utf8'));
      for (const frame of cast.frames) {
        frame.file = path.join(at, frame.file.replace(/\.html$/, '.png'));
        await photograph(page, frame.file.replace(/\.png$/, '.html'), frame.file, { opaque: true, pointer: frame.pointer });
      }
      castToGif(cast, path.join(runner.out, `${name}.gif`), { ...config.gifLook, ...config.get('sessions.gif') });
      console.log(`  ${name}.gif`);
    }
  }
  await browser.close();
}

async function capture(app) {
  const config = loadCaptureConfig(app);
  const runner = makeShotRunner({ config, out: outDir(app) });
  const scenes = config.get('sessions.scenes').filter((scene) => runner.wanted(scene.name));
  if (!scenes.length) return;
  const source = scratchPath(`${app}-sessions`);
  console.log(`${app} terminal sessions`);
  fs.mkdirSync(source, { recursive: true });
  for (const name of fs.readdirSync(source).filter((f) => runner.wanted(f))) {
    fs.rmSync(path.join(source, name), { recursive: true, force: true });
  }
  await record(app, config, scenes);
  await photographAll(config, runner, source);
  await runner.finish();
}

const named = process.argv[2] && !process.argv[2].startsWith('--') ? [process.argv[2]] : ['cli', 'pystencil'];
for (const app of named) await capture(app);
