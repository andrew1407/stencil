// ── §13 registry-driven prompt assembly ─────────────────────────
// The capability set, the forbidden-op boundary, and the §4/§10 prompts assembled from the
// OPS registry — so the prompt can never promise an op this surface cannot run.
import { PROMPT_CORE_HEAD, PROMPT_CORE_TAIL, SCHEMA } from './plan/schema.js';
import { OPS } from './plan/opExecutors.js';

// Every capability the browser chat surface wires, so the shipped prompt promises every
// registered op; a reduced set drops those bullets and §1's unknown-op skip catches them.
export const BROWSER_CAPABILITIES = new Set([
  'loadAttachment', 'saveProject', 'removeProjectNamed', 'clearLocalProjects',
  'renameActiveProject', 'setBlankColor', 'openProjectNamed', 'clearChatConversation',
  'setChatPlacement', 'openDialog', 'setVoiceChat',
]);

// §13 forbidden ops (the registry's forbidden.perSurface.browser list). Two enforcement teeth:
// a test that no OPS key uses one of these names, and the reject in executeOpPlan.
export const FORBIDDEN_OPS = new Set(SCHEMA.forbidden);

// The typed error the executor throws for a forbidden op — even one that somehow
// bypassed the parser (which drops unknown names before they get here).
export class ForbiddenOpError extends Error {
  constructor(op) {
    super(`Forbidden operation "${op}" — this op is never model-drivable (contract §13)`);
    this.name = 'ForbiddenOpError';
    this.op = op;
  }
}

// §13 prompt censor: no generated bullet may read like provider/secret plumbing.
// \btoken\b (not a bare substring) so crop's legitimate "crop tokens" prose passes.
const CENSORED = /api\s*key|bearer|\btoken\b|base\s*url|endpoint/i;

// Registry entry order IS bullet order, "also accepts" lines follow the settings bullets
// (alsoOrder), and entries whose `requires` are not all in `available` are never promised.
export const assemblePrompts = (available = BROWSER_CAPABILITIES) => {
  const coreBullets = [], settingsBullets = [], alsoEntries = [];
  for (const def of Object.values(OPS)) {
    if (def.requires && !def.requires.every((c) => available.has(c))) continue;
    if (def.bullet) (def.editorSetting ? settingsBullets : coreBullets).push(def.bullet);
    if (def.also) alsoEntries.push(def);
  }
  alsoEntries.sort((a, b) => a.alsoOrder - b.alsoOrder);
  const settings = [...settingsBullets, ...alsoEntries.map((d) => d.also)];
  for (const b of [...coreBullets, ...settings]) {
    if (CENSORED.test(b)) throw new Error(`Prompt censor: a registry bullet matches a sensitive pattern (api key/bearer/token/base url/endpoint): ${JSON.stringify(b.slice(0, 60))}`);
  }
  return {
    systemPrompt: PROMPT_CORE_HEAD + coreBullets.join('\n') + PROMPT_CORE_TAIL,
    settingsPrompt: settings.join('\n'),
  };
};

const ASSEMBLED = assemblePrompts();
// Canonical §4 prompt and the §10 editor-settings block, assembled at module load —
// clients may append a short dynamic suffix but never prepend anything.
export const LLM_SYSTEM_PROMPT = ASSEMBLED.systemPrompt;
export const EDITOR_SETTINGS_PROMPT = ASSEMBLED.settingsPrompt;

// §4 + the §10 block at the OP LIST's end, which is the `ask` paragraph's start. The anchor is
// verified: rewording §4 without updating it must fail loudly here.
const SETTINGS_SPLICE_ANCHOR = '\n\nWhen a choice is genuinely';
if (!LLM_SYSTEM_PROMPT.includes(SETTINGS_SPLICE_ANCHOR)) {
  throw new Error('LLM_SYSTEM_PROMPT no longer contains the editor-settings splice anchor');
}
export const EDITOR_SYSTEM_PROMPT = LLM_SYSTEM_PROMPT.replace(
  SETTINGS_SPLICE_ANCHOR,
  `\n${EDITOR_SETTINGS_PROMPT}${SETTINGS_SPLICE_ANCHOR}`);
