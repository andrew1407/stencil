import type { ExportVariant } from './exportVariants.js';

/** Wires the dust-animated variant list behind a copy/download toolbar icon. */
export declare function wireExportOptionsMenu(
  trigger: HTMLElement | null,
  app: object,
  opts: { run: (variant: ExportVariant) => void; currentIcon?: string; hotkeyIds?: Partial<Record<ExportVariant, string>> },
): void;
