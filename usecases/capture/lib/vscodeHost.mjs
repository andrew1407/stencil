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

// Outside the home directory and short on purpose: whatever the terminal prints — the cd, the
// CLI's own path, a saved file — lands in a screenshot, and VS Code's IPC socket path has a
// 103-character cap. config/shared.json `vscode.roots` names it per platform.
const root = (config) => config.vscode.roots[process.platform] || path.join(os.tmpdir(), 'stencil-capture');
export const VS_DIR = (config) => root(config);
export const WORKSPACE = (config) => path.join(root(config), 'ws');
export const CLI_BIN = (config) => path.join(root(config), 'bin', 'stencil');

const SOURCE = path.join(CAPTURE, 'vscode');
const SAMPLE = 'example.stc';
const LANGUAGE_NAME = 'Stencil script';
const WORKBENCH = '.monaco-workbench';

const binaryFor = (config) => process.env.STENCIL_VSCODE
  || path.resolve(config.vscode.binaries[process.platform] || config.vscode.binaries.linux);

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
    fs.copyFileSync(path.join(SOURCE, 'sample', SAMPLE), path.join(WORKSPACE(config), SAMPLE));
    fs.copyFileSync(path.join(REPO, config.shared.urls.localBotIcon), path.join(WORKSPACE(config), 'icon.png'));
    fs.copyFileSync(path.join(REPO, 'cli', 'zig-out', 'bin', 'stencil'), CLI_BIN(config));
    fs.chmodSync(CLI_BIN(config), 0o755);
    const settings = JSON.parse(fs.readFileSync(path.join(SOURCE, 'settings.json'), 'utf8'));
    settings['workbench.colorTheme'] = config.vscode.themes[theme];
    settings['stencil.cliPath'] = CLI_BIN(config);
    // A shell with no rc files and a bare prompt: no user name, no host name, no home path.
    const shell = config.vscode.shells[process.platform] || config.vscode.shells.linux;
    const profile = { Capture: { ...shell, env: { PS1: '$ ', BASH_SILENCE_DEPRECATION_WARNING: '1' } } };
    settings['terminal.integrated.profiles.osx'] = profile;
    settings['terminal.integrated.profiles.linux'] = profile;
    fs.writeFileSync(path.join(dir, 'user', 'User', 'settings.json'), JSON.stringify(settings, null, 2));
  }

  // proc.kill() reaches the main process only: every process on our user-data-dir goes, and
  // the debug port must be free again before the next launch (else VS Code hands off).
  static async freePort(port, dir) {
    try { execFileSync('pkill', ['-9', '-f', `${dir}/user`]); } catch { /* none left */ }
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
