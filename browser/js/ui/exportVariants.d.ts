export type ExportVariant = 'split' | 'current' | 'original' | 'tint';

export declare const EXPORT_VARIANTS: ExportVariant[];
export declare const EXPORT_VARIANT_LABELS: Record<ExportVariant, string>;
export declare const EXPORT_VARIANT_ICONS: Partial<Record<ExportVariant, string>>;

export interface ExportVariantState {
  primary: ExportVariant;
  show: Record<ExportVariant, boolean>;
}

/** Which variant rows exist now, and which owns the primary copy/download combo. */
export declare const exportVariantState: (app: object) => ExportVariantState;
