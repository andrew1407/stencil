// Parsing + validating a model's op plan (llm-contract.md §1, §11, §13): model text → one result
// document, never a throw on model data. The JS twin of core/opplan/planWalk.cpp, pinned
// byte-for-byte by fixtures/opPlan/generated/normalized.json.
import type { PlanAsk } from './opPlan.js';
import type { Schema } from './opSchema.js';

/** A leniency the walk applied; `message` is the canonical wording. */
export interface PlanWarning {
  code: 'W_UNKNOWN_OP' | 'W_VARIANT_DROPPED' | 'W_PREVIEW_DROPPED' | 'W_REPLY_OMITTED';
  op?: string;
  /** 1-based variant or ask option. */
  index?: number;
  label?: string | null;
  message: string;
}

/** Why a plan failed: the canonical message plus the fields a surface re-words from. */
export interface PlanError {
  code: 'E_PLAN' | 'E_ACTION' | 'E_FORBIDDEN' | 'E_JSON_LIMIT';
  rule?: string;
  op?: string;
  scope?: 'actions' | 'variant' | 'option';
  index?: number;
  item?: number;
  isObject?: boolean;
  isString?: boolean;
  max?: number;
  limit?: string;
  detail: string;
  message: string;
}

export interface WalkedAsk {
  question: string;
  mode: string;
  allowCustom: boolean;
  customLabel: string;
  options: Array<{ label: string; actions?: Array<Record<string, unknown>>; image?: Record<string, unknown> }>;
}

export interface PlanResult {
  status: 'valid' | 'chatOnly' | 'invalid';
  reply: string;
  actions: Array<Record<string, unknown>>;
  /** `label` is the model's own, or null when it gave none. */
  variants: Array<{ label: string | null; actions: Array<Record<string, unknown>> }>;
  ask: WalkedAsk | null;
  warnings: PlanWarning[];
  error: PlanError | null;
}

/** The §11 card alone; throws an object carrying `error` (a PlanError) on a malformed card. */
export declare const walkAsk: (schema: Schema, ask: unknown, warnings: PlanWarning[]) => WalkedAsk | null;
export declare const walkPlan: (schema: Schema, text: unknown) => PlanResult;

/** The picked labels joined, or the typed custom text — trimmed and capped to ASK_LIMITS.answer. */
export declare const askAnswerText: (
  ask: PlanAsk,
  answer?: { picked?: ReadonlyArray<string | { label: string }> | string | { label: string }; custom?: string },
) => string;
