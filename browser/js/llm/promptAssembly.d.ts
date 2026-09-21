// §13 registry-driven prompt assembly: the capability set, the forbidden-op boundary and
// the §4/§10 prompts assembled from the OPS registry — so the prompt can never promise an
// op this surface cannot run.

/** Every capability the browser chat surface wires (session.js). */
export declare const BROWSER_CAPABILITIES: Set<string>;
/** The registry's browser forbidden list — never model-drivable. */
export declare const FORBIDDEN_OPS: Set<string>;

/** Thrown by the executor for a forbidden op, even one that bypassed the parser. */
export declare class ForbiddenOpError extends Error {
  constructor(op: string);
  name: 'ForbiddenOpError';
  op: string;
}

export interface AssembledPrompts {
  /** PROMPT_CORE_HEAD + the core op bullets + PROMPT_CORE_TAIL. */
  systemPrompt: string;
  /** The §10 editor-settings bullets plus the "also accepts" lines. */
  settingsPrompt: string;
}

/** Ops whose `requires` are not all in `available` are never promised. Throws on a censored bullet. */
export declare const assemblePrompts: (available?: ReadonlySet<string>) => AssembledPrompts;
/** The canonical §4 prompt — clients may append a short suffix, never prepend. */
export declare const LLM_SYSTEM_PROMPT: string;
export declare const EDITOR_SETTINGS_PROMPT: string;
/** §4 with the §10 block spliced in at the op list's end — what the browser editor sends. */
export declare const EDITOR_SYSTEM_PROMPT: string;
