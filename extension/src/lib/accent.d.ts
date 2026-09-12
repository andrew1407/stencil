// The extension pages' accent facade (window.StencilAccent), assigned by the classic
// script accent.js — no ES exports, so this documents the shape only.

export interface StencilAccentApi {
  readonly list: readonly string[];
  readonly storageKey: string;
  get(): string;
  hexOf(key: string): string;
  inkOn(key: string): boolean;
  set(key: string, from?: unknown): string;
  setCustom(hex: string, from?: unknown): string | null;
  previewAccent(key: string, from?: unknown): void;
  endAccentPreview(from?: unknown): void;
}
