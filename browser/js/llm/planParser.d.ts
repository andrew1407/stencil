// Parsing + validating a model's op plan (contract §1, §11). Model output is DATA:
// everything here rejects or drops, never trusts. The registry decides what an op IS;
// this decides where it may sit.
import type { OpPlan, PlanAsk } from './opPlan.js';

/** §1: a misplaced op inside a variant / ask preview — the caller drops THAT item with a warning. */
export declare class MisplacedOpError extends Error {
  constructor(reason: string);
  name: 'MisplacedOpError';
  reason: string;
}

/** Validate the optional `ask` → the normalised card, or null when absent. Throws "Invalid plan: …". */
export declare const validateAsk: (ask: unknown, warnings: string[]) => PlanAsk | null;

/** The picked labels joined, or the typed custom text — trimmed and capped to ASK_LIMITS.answer. */
export declare const askAnswerText: (
  ask: PlanAsk,
  answer?: { picked?: ReadonlyArray<string | { label: string }> | string | { label: string }; custom?: string },
) => string;

/**
 * Raw LLM reply → validated plan. Fences are stripped and the first balanced JSON object
 * wins; no JSON object at all is a chat-only turn (the raw text is the reply). An invalid
 * plan THROWS; a misplaced op only drops its variant / preview with a warning.
 */
export declare const parseOpPlan: (text: unknown) => OpPlan;
