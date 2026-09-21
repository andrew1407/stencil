// Shapes for llm/prompt.js — §13 system-prompt assembly. The prose core is data
// (src/config/systemPrompt.json); the "Available ops" section is generated from the
// registry, so the prompt can never promise an op this surface does not register.

/** The assembled prompt with every registered op's bullet included. */
export declare const LLM_SYSTEM_PROMPT: string;
/** `exclude` names ops whose runtime capability isn't wired, so their bullets are omitted. */
export declare function buildSystemPrompt(
  registry?: Array<{ name: string; bullet: string }>,
  opts?: { exclude?: Set<string> },
): string;
