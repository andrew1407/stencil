// The <stencil-confirm-modal> asks that are not a plain confirm. Each falls back to what a
// pre-wire or non-DOM caller can safely assume, never to a blocking native dialog.

/** The chosen option value, or null on cancel; no modal resolves the first option. */
export declare const askChoose: (el: HTMLElement | null,
  message: string, opts?: { options?: { value: string; label: string }[]; title?: string }) => Promise<string | null>;
/** 'confirm' | 'alt' | null; no modal resolves null. */
export declare const askAlt: (el: HTMLElement | null,
  message: string, opts?: { title?: string; confirmLabel?: string; altLabel?: string }) => Promise<string | null>;
/** The trimmed string, or null on cancel; no modal resolves `defaultValue`. */
export declare const askPrompt: (el: HTMLElement | null,
  message: string, opts?: { title?: string; confirmLabel?: string; defaultValue?: string }) => Promise<string | null>;
