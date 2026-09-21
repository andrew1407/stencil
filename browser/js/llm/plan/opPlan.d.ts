// Shapes for llm/plan.js — the validated op plan and its executor (llm-contract.md
// §1–§4, §8, §11). Model output is DATA: nothing here is trusted until parseOpPlan has
// returned it, and executeOpPlan maps each op 1:1 onto a window.stencil facade call.
import type { Stencil } from '../../console/stencilApi.js';
import type { createSchema } from './opSchema.js';

export {
  PROMPT_CORE_HEAD, PROMPT_CORE_TAIL, SCHEMA, LIMITS, ASK_LIMITS, DEFAULT_CUSTOM_LABEL,
} from './schema.js';
export { sanitizeLabel, resolveServer } from './values.js';
export { OPS } from './opExecutors.js';
export {
  BROWSER_CAPABILITIES, FORBIDDEN_OPS, ForbiddenOpError, assemblePrompts,
  LLM_SYSTEM_PROMPT, EDITOR_SETTINGS_PROMPT, EDITOR_SYSTEM_PROMPT,
} from '../promptAssembly.js';
export { MisplacedOpError, validateAsk, askAnswerText, parseOpPlan } from './parser.js';

/** The registry-filtered validator schema.js builds for the browser profile. */
export type Schema = ReturnType<typeof createSchema>;

/** One validated action; `op` is always a name the browser profile registers. */
export interface PlanAction { op: string; [key: string]: unknown; }

/** A variant renders an image from a branch of actions and puts the editor back. */
export interface PlanVariant { label: string; actions: PlanAction[]; }

/** §11: one option on a choice card — a preview render, an image reference, or a label. */
export interface AskOption {
  label: string;
  actions?: PlanAction[];
  image?: { url?: string; projectId?: string; scanIndex?: number };
}

export interface PlanAsk {
  question: string;
  options: AskOption[];
  multi?: boolean;
  custom?: boolean;
  customLabel?: string;
}

/** What parseOpPlan returns; it throws only on a plan that is malformed beyond repair. */
export interface OpPlan {
  reply: string;
  actions: PlanAction[];
  variants: PlanVariant[];
  ask: PlanAsk | null;
  /** Every leniency applied — a dropped op, a dropped variant, a substituted reply. */
  warnings: string[];
  /** true = the model answered prose, not JSON; `actions` is empty. */
  chatOnly: boolean;
}

export interface VariantResult { label: string; dataUrl: string; }
export interface ExecuteResult { results: VariantResult[]; warnings: string[]; }

/** A saved server the §10 connect op may resolve against; plans never carry tokens. */
export type SavedServerEntry = string | { url: string; token?: string };

/** An executor's note: what it did, or null when nothing happened. */
export type CapabilityNote = string | null;

/** The surface's injected capabilities; an absent one makes its op error or note+skip. */
export interface ExecuteOptions {
  exportImage?: () => Promise<string>;
  /** Load the working video's frame `index` into the editor (§2 frame). */
  loadFrame?: (index: number) => Promise<void>;
  /** The pool §10 `connect` resolves in — the user's saved servers, never a model-named host. */
  savedServers?: () => SavedServerEntry[];
  /** What the user typed this turn; the openUrl guard reads it. */
  userText?: () => string;
  openIncognito?: (url: string) => Promise<void>;
  loadAttachment?: (index: number) => Promise<void>;
  saveProject?: (name?: string | null) => Promise<string>;
  copyRendered?: () => Promise<unknown>;
  copyLayoutRendered?: () => Promise<unknown>;
  removeProjectNamed?: (name: string) => Promise<CapabilityNote>;
  clearWorkingImage?: () => Promise<CapabilityNote>;
  clearLocalProjects?: (keepCurrent: boolean) => Promise<CapabilityNote>;
  renameActiveProject?: (name: string) => Promise<CapabilityNote>;
  setBlankColor?: (color: string) => Promise<CapabilityNote>;
  openProjectNamed?: (name: string, last: boolean) => Promise<CapabilityNote>;
  clearChatConversation?: () => Promise<CapabilityNote>;
  setChatPlacement?: (placement: { open?: boolean | null; dock?: string | null }) => Promise<CapabilityNote>;
  /** null closes whatever dialog is open. */
  openDialog?: (name: string | null) => Promise<CapabilityNote>;
  setVoiceChat?: (on: boolean) => CapabilityNote;
  /** Takes the deferred (§10 clearChat) actions UNEXECUTED for the turn runner to replay. */
  deferredSink?: PlanAction[];
  /** Collects the top-level actions that completed — what a preview sandbox undoes. */
  ranSink?: PlanAction[];
}

/** Runs the plan against the frozen facade; variants branch from the post-actions state. */
export declare const executeOpPlan: (plan: Pick<OpPlan, 'actions' | 'variants' | 'warnings'>, stencil: Stencil, opts?: ExecuteOptions) => Promise<ExecuteResult>;

export interface AskPreview { index: number; label: string; dataUrl: string; }

/** One option's failed render costs that option its picture, never the card. */
export declare const renderAskPreviews: (
  ask: PlanAsk | null | undefined, stencil: Stencil, opts?: Pick<ExecuteOptions, 'exportImage' | 'loadFrame'>,
) => Promise<{ previews: AskPreview[]; warnings: string[] }>;
