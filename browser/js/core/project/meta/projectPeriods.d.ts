// Project expiry: the refresh presets, the arithmetic over them and the expired /
// expiring-soon predicates; twin of core/state/ProjectsStore.cpp's period helpers.
import type { ProjectMeta, RefreshPeriod } from '../store/projectsStore.js';

/** One week in ms (also the "week" preset). */
export declare const EXPIRY_MS: number;
/** Warn once a project is within a day of expiry. */
export declare const WARN_MS: number;
export declare const DEFAULT_PERIOD: RefreshPeriod;
export declare const PERIOD_MS: Record<RefreshPeriod, number>;
export declare const PERIOD_ORDER: readonly RefreshPeriod[];

/** Unknown/empty → one week. */
export declare const periodMs: (period: string | null | undefined) => number;
export declare const addPeriod: (from: number, period: string | null | undefined) => number;

/** expiresAt of 0 (or absent) == keep forever. */
export declare const isExpired: (meta: ProjectMeta | null | undefined, now: number) => boolean;
export declare const expiresAt: (meta: ProjectMeta | null | undefined) => number | null;
/** Due within WARN_MS; an already-expired project is false. */
export declare const isExpiringSoon: (meta: ProjectMeta | null | undefined, now: number) => boolean;
