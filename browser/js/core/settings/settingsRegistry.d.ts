/** One simple setting, as data: where it writes, how it parses, what it mirrors and commits. */
export interface SettingDescriptor {
  field: string;
  parse?: (raw: unknown) => unknown;
  mirror?: Array<{ id: string; kind: string }>;
  afterSet?: (controller: unknown, value: unknown) => void;
  redraw?: boolean;
  save?: boolean;
  remoteSync?: boolean;
  filterDirty?: boolean;
}

/** Compare-view modes in cycle order (Alt+O steps through them). */
export declare const COMPARE_MODES: readonly string[];

/** The registry SettingsController.set(key, value) drives. */
export declare const SETTINGS: Record<string, SettingDescriptor>;
