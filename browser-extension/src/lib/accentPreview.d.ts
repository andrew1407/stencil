export interface AccentApi {
  get(): string;
  set(key: string, from?: unknown): string;
  previewAccent(key: string, from?: unknown): void;
  endAccentPreview(from?: unknown): void;
}

export interface AccentPreviewOptions {
  menu: HTMLElement;
  logo: unknown;
  accent: AccentApi;
  closeMenu: () => void;
}

export interface AccentPreview {
  clearHover(): void;
  markSel(): void;
  onRowEnter(li: HTMLElement, key: string): void;
  pick(key: string): void;
  restore(): void;
  scheduleRestore(): void;
}

export declare const createAccentPreview: (opts: AccentPreviewOptions) => AccentPreview;
