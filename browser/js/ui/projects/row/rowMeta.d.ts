export interface ExpiryLabel {
  text: string;
  expired: boolean;
  soon: boolean;
}

export interface RowMeta {
  fmtDate(ts: number | null | undefined): string;
  /** Size, orientation and description — the row's hover text. */
  projectTooltip(meta: Record<string, unknown>): string;
  expiryLabel(meta: Record<string, unknown>): ExpiryLabel;
}

/** The strings a project row states, read from the projects store. */
export declare function createRowMeta(store: object): RowMeta;
