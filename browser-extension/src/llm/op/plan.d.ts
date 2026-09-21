// Shapes for llm/plan.js — the validated op plan (llm-contract.md §1–§4, §8, §11).
// Model output is DATA: nothing here is trusted until parseOpPlan has returned it.

/** One validated action. `op` is always a name the extension profile registers. */
export interface PlanAction {
  op: string;
  [key: string]: unknown;
}

/** §11 interactive reply: a question with bounded options, optionally free-text. */
export interface PlanAsk {
  question: string;
  options: string[];
  multi?: boolean;
  custom?: boolean;
  customLabel?: string;
}

/** What parseOpPlan returns. It throws only on a plan that is malformed beyond repair. */
export interface OpPlan {
  /** The prose shown in the transcript; substituted when the model omitted it. */
  reply: string;
  actions: PlanAction[];
  ask: PlanAsk | null;
  /** Every leniency applied — a dropped op, a dropped variant, a substituted reply. */
  warnings: string[];
  /** true = the model answered prose, not JSON; `actions` is empty. */
  chatOnly: boolean;
}

/** The measurement context a plan's indices are validated against. */
export interface PlanContext {
  listingLength?: number;
  tabsLength?: number;
}

export declare const LIMITS: Record<string, number>;
export declare const ASK_LIMITS: Record<string, number>;
export declare const DEFAULT_CUSTOM_LABEL: string;
export declare const FORBIDDEN_OPS: Set<string>;
export declare const OP_REGISTRY: Array<{ op: string; [key: string]: unknown }>;
export declare const LLM_SYSTEM_PROMPT: string;
export declare const SCHEMA: unknown;
export declare function buildSystemPrompt(registry?: unknown, opts?: { exclude?: Set<string> }): string;
export declare function validateAsk(ask: unknown, listingLength: number, warnings: string[]): PlanAsk | null;
export declare function askAnswerText(ask: PlanAsk, answer?: { picked?: string[]; custom?: string }): string;
export declare function parseOpPlan(text: string, context?: PlanContext): OpPlan;
/** The plan's only actionable output is `attach` — the single bounded auto-continuation. */
export declare function attachOnly(plan: OpPlan | null | undefined): boolean;
/** Every action only GATHERS context, so another model round is needed to act on it. */
export declare function continuationOnly(plan: OpPlan | null | undefined): boolean;
