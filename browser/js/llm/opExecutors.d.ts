// The op executors: one per registered op (contract §13). run(a, ctx) executes a VALIDATED
// action against the frozen window.stencil facade — never new editor logic. Everything
// else about an op is its registry entry's; the table is built from SCHEMA.entries.
import type { Stencil } from '../console/stencilApi.js';
import type { ExecuteOptions, PlanAction, VariantResult } from './opPlan.js';
import type { Frame } from './frame.js';

/** What every executor receives: the facade, the injected capabilities, and the run's state. */
export interface OpContext extends ExecuteOptions {
  stencil: Stencil;
  /** Rendered outputs: variants, and multi-index frame picks. */
  results: VariantResult[];
  /** What an executor did or skipped — rendered with the reply as warnings. */
  notes: string[];
  /** The §1 re-mapping accumulated from executed crops/rotates; reset by `newFrame` ops. */
  frame: Frame;
}

export type OpRunner = (a: PlanAction, ctx: OpContext) => void | Promise<void>;

export interface OpDefinition {
  /** The prompt bullet promising this op, or null for a silent op. */
  bullet: string | null;
  /** The table-driven check + normalize; throws "Invalid <op> action: …". */
  validate(a: unknown): PlanAction;
  run: OpRunner;
  also?: string;
  alsoOrder?: number;
  /** Capabilities the surface must inject before the op is promised in the prompt. */
  requires?: string[];
  /** §10: adjusts the EDITOR, not the image — forbidden inside variants and previews. */
  editorSetting?: true;
  topLevelOnly?: true;
  /** Loads a new picture: the §1 frame resets to identity after it. */
  newFrame?: true;
  /** Runs at the plan's END (clearChat, dialog). */
  deferred?: true;
}

/** One entry per whitelisted op, keyed by op name, in registry (= prompt) order. */
export declare const OPS: Record<string, OpDefinition>;
