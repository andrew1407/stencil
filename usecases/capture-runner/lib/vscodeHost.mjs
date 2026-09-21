// A throwaway VS Code: the extension from the tree, a fresh user-data-dir with our
// settings, driven over its Chromium debug port. Shared by vscodeExtension.mjs (the editor)
// and cliTerminal.mjs (the real terminal the CLI console is photographed in).
import { spawn, execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { chromium } from './playwright.mjs';
import { CAPTURE, REPO } from './paths.mjs';
import { waitForStable } from './waits.mjs';

const WIN = process.platform === 'win32';

// Outside the home directory and short on purpose: the terminal's output lands in a screenshot,
// and VS Code's IPC socket path has a 103-character cap (config/shared.json `vscode.roots`).
const root = (config) => config.vscode.roots[process.platform] || path.join(os.tmpdir(), 'stencil-capture');
export const VS_DIR = (config) => root(config);
export const WORKSPACE = (config) => path.join(root(config), 'ws');
const CLI_NAME = WIN ? 'stencil.exe' : 'stencil';
export const CLI_BIN = (config) => path.join(root(config), 'bin', CLI_NAME);

// The terminal shots type real shell lines, and cmd.exe shares none of bash's spelling.
// `$$$S` is cmd's PROMPT for "$ ", so either shell prints the same bare prompt.
export const SHELL = WIN
  ? { env: { PROMPT: '$$$S' },
      setEnv: (v) => Object.entries(v).map(([n, x]) => `set ${n}=${x}`).join(' & '),
      clearThen: (cmd) => `cls & ${cmd}` }
  : { env: { PS1: '$ ', BASH_SILENCE_DEPRECATION_WARNING: '1' },
      setEnv: (v) => `export ${Object.entries(v).map(([n, x]) => `${n}=${x}`).join(' ')}`,
      clearThen: (cmd) => `clear; ${cmd}` };

const SOURCE = path.join(CAPTURE, 'vscode');
export const SAMPLES = ['example.stc', 'example.stcjs', 'example.pystc', 'example.stencil'];
const SAMPLE = SAMPLES[0];
const LANGUAGE_NAME = 'Stencil script';
const WORKBENCH = '.monaco-workbench';

// config/shared.json spells the Windows path with %LOCALAPPDATA%; no one else expands it.
const expandVars = (p) => p.replace(/%([^%]+)%/g, (_, name) => process.env[name] ?? '');
const binaryFor = (config) => process.env.STENCIL_VSCODE
  || path.resolve(expandVars(config.vscode.binaries[process.platform] || config.vscode.binaries.linux));

export class VsCodeHost {
  #proc;
  #page;
  #config;

  constructor(proc, page, config) {
    this.#proc = proc;
    this.#page = page;
    this.#config = config;
  }

  get page() { return this.#page; }

  // A private state dir, our settings with the run's theme, the sample workspace, and a copy
  // of the CLI under that same neutral root, so the terminal never prints a home path.
  static prepare(config, theme) {
    const dir = root(config);
    fs.rmSync(dir, { recursive: true, force: true });
    for (const sub of ['user/User', 'ext', 'ws/out', 'bin']) fs.mkdirSync(path.join(dir, sub), { recursive: true });
    for (const name of SAMPLES) fs.copyFileSync(path.join(SOURCE, 'sample', name), path.join(WORKSPACE(config), name));
    fs.copyFileSync(path.join(REPO, config.shared.urls.localBotIcon), path.join(WORKSPACE(config), 'icon.png'));
    fs.copyFileSync(path.join(REPO, 'cli', 'zig-out', 'bin', CLI_NAME), CLI_BIN(config));
    if (!WIN) fs.chmodSync(CLI_BIN(config), 0o755);
    const settings = JSON.parse(fs.readFileSync(path.join(SOURCE, 'settings.json'), 'utf8'));
    settings['workbench.colorTheme'] = config.vscode.themes[theme];
    settings['stencil.cliPath'] = CLI_BIN(config);
    // A shell with no rc files and a bare prompt: no user name, no host name, no home path.
    const shell = config.vscode.shells[process.platform] || config.vscode.shells.linux;
    // PYTHONPATH so a .pystc finds pystencil the way an installed one would; the path is in
    // the environment, never on the command line the terminal shot carries.
    const env = { ...SHELL.env, PYTHONPATH: path.join(REPO, 'pystencil') };
    const profile = { Capture: { ...shell, env } };
    for (const slot of ['osx', 'linux', 'windows']) {
      settings[`terminal.integrated.profiles.${slot}`] = profile;
      settings[`terminal.integrated.defaultProfile.${slot}`] = 'Capture';
    }
    fs.writeFileSync(path.join(dir, 'user', 'User', 'settings.json'), JSON.stringify(settings, null, 2));
  }

  // proc.kill() reaches the main process only: every process on our user-data-dir goes, and
  // the debug port must be free again before the next launch (else VS Code hands off).
  static async freePort(port, dir) {
    const held = path.join(dir, 'user');
    try {
      if (WIN) {
        execFileSync('powershell', ['-NoProfile', '-Command', 'Get-CimInstance Win32_Process'
          + ` | Where-Object { $_.CommandLine -like '*${held}*' }`
          + ' | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }'], { stdio: 'ignore' });
      } else {
        execFileSync('pkill', ['-9', '-f', held]);
      }
    } catch { /* none left */ }
    for (let i = 0; i < 40; i++) {
      const busy = await fetch(`http://127.0.0.1:${port}/json/version`).then(() => true, () => false);
      if (!busy) return;
      await delay(250);
    }
    throw new Error(`port ${port} is still held by another VS Code`);
  }

  static async launch(config, theme, { open = SAMPLE } = {}) {
    const port = config.vscode.debugPort;
    const dir = root(config);
    await VsCodeHost.freePort(port, dir);
    VsCodeHost.prepare(config, theme);
    const file = open ? path.join(WORKSPACE(config), open) : null;
    const proc = spawn(binaryFor(config), [
      `--extensionDevelopmentPath=${path.join(REPO, 'vscode-extension')}`, `--user-data-dir=${dir}/user`,
      `--extensions-dir=${dir}/ext`, `--remote-debugging-port=${port}`, '--disable-workspace-trust',
      '--skip-welcome', '--skip-release-notes', '--new-window', WORKSPACE(config), ...(file ? [file] : []),
    ], { stdio: 'ignore' });
    let browser = null;
    for (let i = 0; i < 40 && !browser; i++) {
      await delay(500);
      browser = await chromium.connectOverCDP(`http://127.0.0.1:${port}`).catch(() => null);
    }
    if (!browser) throw new Error('VS Code never opened its debug port');
    const page = browser.contexts().flatMap((ctx) => ctx.pages()).find((p) => p.url().includes('workbench'));
    const cdp = await page.context().newCDPSession(page);
    const { width, height } = config.vscode.window;
    await cdp.send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
    const host = new VsCodeHost(proc, page, config);
    await host.waitUntilReady(Boolean(open));
    return host;
  }

  // Ready means the workbench painted and — with a file open — the extension host answered:
  // the status bar names our language and the colouring has stopped changing.
  async waitUntilReady(hasEditor) {
    await this.#page.locator(WORKBENCH).first().waitFor({ timeout: 30_000 });
    await this.#page.locator('.statusbar').first().waitFor({ timeout: 30_000 });
    if (!hasEditor) {
      await delay(this.#config.vscode.settleMs);
      return;
    }
    await this.#page.locator('.statusbar', { hasText: LANGUAGE_NAME }).first().waitFor({ timeout: 30_000 });
    await waitForStable(this.#page, () => document.querySelector('.view-lines')?.innerHTML?.length ?? 0,
      { idleMs: 600, timeoutMs: 20_000 });
  }

  // Quick Open by name, so a step can move between the workspace's files without a dialog.
  async openFile(name) {
    await this.#page.keyboard.press('F1');
    const input = this.#page.locator('.quick-input-widget input');
    await input.waitFor({ timeout: 10_000 });
    await input.fill(name);
    await this.#page.locator('.quick-input-list .monaco-list-row').first().waitFor({ timeout: 10_000 });
    await this.#page.keyboard.press('Enter');
    await this.#page.locator('.quick-input-widget').waitFor({ state: 'hidden', timeout: 10_000 }).catch(() => {});
    await waitForStable(this.#page, () => document.querySelector('.view-lines')?.innerHTML?.length ?? 0,
      { idleMs: 600, timeoutMs: 20_000 });
  }

  async runCommand(command) {
    await this.#page.keyboard.press('F1');
    const input = this.#page.locator('.quick-input-widget input');
    await input.waitFor({ timeout: 10_000 });
    await input.fill(`>${command}`);
    await this.#page.locator('.quick-input-list .monaco-list-row').first().waitFor({ timeout: 10_000 });
    await this.#page.keyboard.press('Enter');
    await this.#page.locator('.quick-input-widget').waitFor({ state: 'hidden', timeout: 10_000 }).catch(() => {});
  }

  async stop() {
    this.#proc.kill();
    await VsCodeHost.freePort(this.#config.vscode.debugPort, root(this.#config));
  }
}
