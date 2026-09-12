/** A parsed layout payload (expects a `lines` array). */
export type LayoutPayload = Record<string, unknown>;

/** A JSON-layout file chosen in a file input: validate, confirm, install. */
export declare function uploadJSON(app: Record<string, unknown>, e: unknown): void;

/** Install a pasted or dropped layout, with the replace / combine / dimension prompts.
 *  `from` is the drop point — the canvas flies in out of it. */
export declare function applyPastedLayout(
  app: Record<string, unknown>, data: LayoutPayload, from?: unknown,
): Promise<void>;

/** Install a layout with NO prompt and NO toast (the path behind stencil.setLines()).
 *  Returns true when it was applied. */
export declare function installLayout(
  app: Record<string, unknown>, data: LayoutPayload, opts?: { mode?: string; history?: boolean },
): boolean;
