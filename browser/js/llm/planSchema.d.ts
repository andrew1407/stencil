// Op-plan schema + limits (llm-contract.md §1, §4, §11, §13): the registry filtered to the
// browser profile, the prose core of the system prompt, and the caps every client shares.
import type { Schema } from './opSchema.js';

/** Contract §1 caps, read from the registry — the same numbers in every client. */
export interface PlanLimits {
  actions: number; variants: number; layoutLines: number;
  frameIndices: number; stringChars: number; pathChars: number;
}

/** §11 ask caps: option count window and the text caps that keep a card a card. */
export interface AskLimits { minOptions: number; maxOptions: number; question: number; label: number; answer: number; }

/** The prose core of the §4 prompt, embedded verbatim around the assembled op bullets. */
export declare const PROMPT_CORE_HEAD: string;
export declare const PROMPT_CORE_TAIL: string;
export declare const SCHEMA: Schema;
export declare const LIMITS: PlanLimits;
export declare const ASK_LIMITS: AskLimits;
export declare const DEFAULT_CUSTOM_LABEL: string;
