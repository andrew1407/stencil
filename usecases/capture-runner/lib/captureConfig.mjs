// The declarative half of a capture: config/shared.json merged with config/<app>.json,
// deep-frozen and read through dotted keys. Nothing here touches a browser or a file the
// apps own — the scripts hold gestures, this holds constants.
import fs from 'node:fs';
import path from 'node:path';
import { CONFIG_DIR, SCRATCH } from './paths.mjs';

const SHARED_FILE = 'shared.json';

const deepFreeze = (value) => {
  if (!value || typeof value !== 'object' || Object.isFrozen(value)) return value;
  for (const inner of Object.values(value)) deepFreeze(inner);
  return Object.freeze(value);
};

const readJson = (file) => deepFreeze(JSON.parse(fs.readFileSync(path.join(CONFIG_DIR, file), 'utf8')));

const dig = (root, dotted) => dotted.split('.').reduce((node, key) => (node == null ? node : node[key]), root);

export class CaptureConfig {
  #app;
  #shared;
  #own;

  constructor(app, shared, own) {
    this.#app = app;
    this.#shared = shared;
    this.#own = own;
  }

  // The named app's config over the shared one; both stay readable on their own.
  static of(app) {
    return new CaptureConfig(app, readJson(SHARED_FILE), readJson(`${app}.json`));
  }

  get app() { return this.#app; }
  get shared() { return this.#shared; }
  get own() { return this.#own; }
  get budget() { return this.#shared.budget; }
  get gifLook() { return this.#shared.gif; }
  get vscode() { return this.#shared.vscode; }
  get serverUrl() { return process.env.STENCIL_DOCS_SERVER_URL || this.#shared.server.url; }

  // The app's own value, falling back to the shared tree, then to `fallback`.
  get(dotted, fallback = undefined) {
    const own = dig(this.#own, dotted);
    if (own !== undefined) return own;
    const shared = dig(this.#shared, dotted);
    return shared === undefined ? fallback : shared;
  }

  // The theme block an app declares, over the shared defaults.
  get theme() {
    return { ...this.#shared.theme, ...this.#own.theme, steps: this.#own.theme?.steps || {} };
  }

  prompt(key) { return this.#shared.prompts[key]; }

  stubPlan(key) { return this.#shared.stub[key]; }

  // The repo's own files over public URLs (raw.githubusercontent.com answers CORS; the
  // github.com redirect does not).
  url(key) { return this.#shared.urls.raw + this.#shared.urls[key]; }

  // A real assistant, when a collaboration server with an LLM proxy is at hand: the token
  // from the env, else the scratch file `mint` writes. Absent, the chat shots use the stub.
  serverToken() {
    if (process.env.STENCIL_DOCS_SERVER_TOKEN) return process.env.STENCIL_DOCS_SERVER_TOKEN;
    try {
      return fs.readFileSync(path.join(SCRATCH, this.#shared.server.tokenFile), 'utf8').trim() || null;
    } catch {
      return null;
    }
  }
}

export const loadCaptureConfig = (app) => CaptureConfig.of(app);
