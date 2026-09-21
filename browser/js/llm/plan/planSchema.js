// ── Op-plan schema + limits (llm-contract.md §1, §4, §11, §13) ──
// The table every op-plan module validates against, extracted from opPlan.js.
import { createSchema } from './opSchema.js';
import PROMPT_ASSET from '../../config/llm/systemPrompt.json' with { type: 'json' };
import REGISTRY from '../../config/llm/opRegistry.json' with { type: 'json' };

// Canonical system prompt (§4 + §13): the PROSE CORE is embedded verbatim (append-only,
// never prepend) while the ops section + §10 block are ASSEMBLED from the OPS registry.
export const PROMPT_CORE_HEAD = PROMPT_ASSET.head;
export const PROMPT_CORE_TAIL = PROMPT_ASSET.tail;

// config/llm/opRegistry.json filtered to the browser profile (§13): membership, key
// schemas, caps, token grammars and the cross-field rules all come from the registry.
export const SCHEMA = createSchema(REGISTRY, 'browser');

// Limits — the same numbers in every client (contract §1), read from the registry.
export const LIMITS = Object.freeze({
  actions: SCHEMA.limits.MAX_ACTIONS, variants: SCHEMA.limits.MAX_VARIANTS,
  layoutLines: SCHEMA.limits.MAX_LAYOUT_LINES, frameIndices: SCHEMA.limits.MAX_FRAME_INDICES,
  stringChars: SCHEMA.limits.MAX_STRING_CHARS, pathChars: SCHEMA.limits.MAX_PATH_CHARS,
});

// §11 interactive replies: the option cap is what a choice card can show without becoming
// a menu, and the text caps keep a model-written card from filling the transcript.
export const ASK_LIMITS = Object.freeze({ ...SCHEMA.limits.ask });
export const DEFAULT_CUSTOM_LABEL = REGISTRY.ask.defaultCustomLabel;
