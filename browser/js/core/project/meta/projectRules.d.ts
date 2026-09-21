/** Only expiresAt is read; absent / 0 means "keep forever". */
export interface ExpiringMeta { expiresAt?: number | null; }

/** C export symbols the project rules cwrap, verified before any wrapper installs. */
export const projectRuleExports: string[];

/** Built over an instantiated Emscripten module; keyed like the other core ops. */
export function buildProjectRules(mod: unknown): {
  projectPeriodMs(period?: string | null): number;
  projectAddPeriod(from: number, period?: string | null): number;
  projectShouldPersist(activeId: string | null | undefined, temporary: boolean): boolean;
  projectIsExpired(meta: ExpiringMeta | null, now: number): boolean;
  projectIsExpiringSoon(meta: ExpiringMeta | null, now: number): boolean;
};
